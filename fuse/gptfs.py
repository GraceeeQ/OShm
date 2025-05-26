#!/usr/bin/env python3
# filepath: gptfs.py

import os
import sys
import errno
import stat
import time
import logging
import requests
import json
from collections import defaultdict
from fuse import FUSE, FuseOSError, Operations
from threading import Lock
from dotenv import load_dotenv

# 加载环境变量，用于 API 密钥
load_dotenv()

# 配置日志
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger('GPTfs')

class GPTfs(Operations):
    def __init__(self):
        self.files = {}
        self.data = defaultdict(bytes)
        self.fd = 0
        self.api_lock = Lock()
        
        # API 配置
        self.api_key = os.getenv('deepseek-api-key')
        self.api_url = "https://api.deepseek.com/v1/chat/completions"  # DeepSeek 的 API 端点
        self.model = "deepseek-chat"  # DeepSeek 的模型名称
    
        # 存储目录结构
        self.directories = set('/')
        # 存储目录中的文件
        self.dir_files = defaultdict(set)
        
        # 初始化根目录
        now = time.time()
        self.files['/'] = dict(
            st_mode=(stat.S_IFDIR | 0o755),
            st_ctime=now,
            st_mtime=now,
            st_atime=now,
            st_nlink=2
        )
        
    def _full_path(self, partial):
        if partial.startswith("/"):
            return partial
        else:
            return "/" + partial
    
    def _is_session_dir(self, path):
        """检查路径是否为会话目录"""
        parts = path.strip('/').split('/')
        return len(parts) == 1 and path != '/'
    
    def _is_special_file(self, path):
        """检查文件是否为特殊文件 (input/output/error)"""
        parts = path.strip('/').split('/')
        return len(parts) == 2 and parts[1] in ['input', 'output', 'error']
    
    def _get_session_dir(self, path):
        """从路径获取会话目录"""
        parts = path.strip('/').split('/')
        if len(parts) >= 1:
            return '/' + parts[0]
        return None
    
    def _call_gpt_api(self, prompt):
        """调用 GPT API 并返回回复"""
        if not self.api_key:
            return b"Error: API key not found. Please set OPENAI_API_KEY environment variable."
        
        try:
            from openai import OpenAI
        
            # 创建 DeepSeek 客户端
            client = OpenAI(
                api_key=self.api_key,
                base_url="https://api.deepseek.com"
            )

            # 解码 prompt
            prompt_text = prompt.decode('utf-8', errors='ignore')

            # 调用 API
            response = client.chat.completions.create(
                model=self.model,
                messages=[
                    {"role": "system", "content": "You are a helpful assistant"},
                    {"role": "user", "content": prompt_text}
                ],
                stream=False
            )

            # 获取回复
            message = response.choices[0].message.content
            return message.encode('utf-8')
        except Exception as e:
            error_msg = f"Exception when calling API: {str(e)}"
            logger.error(error_msg)
            return f"Error: {str(e)}".encode('utf-8')
    
    def chmod(self, path, mode):
        self.files[path]['st_mode'] &= 0o770000
        self.files[path]['st_mode'] |= mode
        return 0
    
    def chown(self, path, uid, gid):
        self.files[path]['st_uid'] = uid
        self.files[path]['st_gid'] = gid
    
    def create(self, path, mode):
        path = self._full_path(path)
        
        # 只允许创建特殊文件
        if not self._is_special_file(path):
            raise FuseOSError(errno.EPERM)
        
        self.files[path] = dict(
            st_mode=(stat.S_IFREG | mode),
            st_nlink=1,
            st_size=0,
            st_ctime=time.time(),
            st_mtime=time.time(),
            st_atime=time.time()
        )
        
        self.fd += 1
        session_dir = self._get_session_dir(path)
        if session_dir:
            self.dir_files[session_dir].add(os.path.basename(path))
        
        return self.fd
    
    def getattr(self, path, fh=None):
        path = self._full_path(path)
        
        if path in self.files:
            return self.files[path]
        
        parts = path.strip('/').split('/')
        
        # 检查是否是会话目录下的特殊文件
        if len(parts) == 2 and parts[1] in ['input', 'output', 'error']:
            session_dir = '/' + parts[0]
            if session_dir in self.files:  # 如果会话目录存在
                # 为特殊文件创建属性
                now = time.time()
                attr = dict(
                    st_mode=(stat.S_IFREG | 0o666),
                    st_nlink=1,
                    st_size=len(self.data.get(path, b'')),
                    st_ctime=now,
                    st_mtime=now,
                    st_atime=now
                )
                self.files[path] = attr
                self.dir_files[session_dir].add(parts[1])
                return attr
        
        raise FuseOSError(errno.ENOENT)
    
    def getxattr(self, path, name, position=0):
        return ''  # 不支持扩展属性
    
    def listxattr(self, path):
        return []  # 不支持扩展属性列表
    
    def mkdir(self, path, mode):
        path = self._full_path(path)
        parts = path.strip('/').split('/')
        
        # 只允许在根目录下创建会话目录
        if len(parts) != 1 or path == '/':
            raise FuseOSError(errno.EPERM)
        
        now = time.time()
        self.files[path] = dict(
            st_mode=(stat.S_IFDIR | mode),
            st_nlink=2,  # 目录链接数至少为2 (.和..)
            st_size=0,
            st_ctime=now,
            st_mtime=now,
            st_atime=now
        )
        
        self.directories.add(path)
        
        # 为新会话目录预创建特殊文件
        for special_file in ['input', 'output', 'error']:
            file_path = os.path.join(path, special_file)
            self.files[file_path] = dict(
                st_mode=(stat.S_IFREG | 0o666),
                st_nlink=1,
                st_size=0,
                st_ctime=now,
                st_mtime=now,
                st_atime=now
            )
            self.data[file_path] = b''
            self.dir_files[path].add(special_file)
        
        return 0
    
    def open(self, path, flags):
        self.fd += 1
        return self.fd
    
    def read(self, path, size, offset, fh):
        path = self._full_path(path)
        
        # 确保文件存在
        if path not in self.files:
            raise FuseOSError(errno.ENOENT)
        
        # 如果是特殊文件，返回其内容
        if self._is_special_file(path):
            return self.data.get(path, b'')[offset:offset + size]
        
        raise FuseOSError(errno.EPERM)
    
    def readdir(self, path, fh):
        path = self._full_path(path)
        
        # 基本目录条目
        entries = ['.', '..']
        
        if path == '/':
            # 根目录显示所有会话目录
            entries.extend([d.strip('/') for d in self.directories if d != '/'])
        elif path in self.directories:
            # 会话目录显示特殊文件
            entries.extend(self.dir_files.get(path, []))
        
        return entries
    
    def readlink(self, path):
        return self.data[path].decode('utf-8')
    
    def removexattr(self, path, name):
        pass  # 不支持扩展属性
    
    def rename(self, old, new):
        # 不支持重命名
        raise FuseOSError(errno.EPERM)
    
    def rmdir(self, path):
        path = self._full_path(path)
        
        # 检查是否为会话目录
        if not self._is_session_dir(path):
            raise FuseOSError(errno.EPERM)
        
        # 删除目录下的所有文件
        for file_name in list(self.dir_files.get(path, [])):
            file_path = os.path.join(path, file_name)
            if file_path in self.files:
                del self.files[file_path]
            if file_path in self.data:
                del self.data[file_path]
        
        # 清理目录记录
        self.directories.remove(path)
        del self.dir_files[path]
        del self.files[path]
        
        return 0
    
    def setxattr(self, path, name, value, options, position=0):
        # 不支持扩展属性
        pass
    
    def statfs(self, path):
        return dict(f_bsize=512, f_blocks=4096, f_bavail=2048)
    
    def symlink(self, target, source):
        # 不支持符号链接
        raise FuseOSError(errno.EPERM)
    
    def truncate(self, path, length, fh=None):
        path = self._full_path(path)
        
        # 只能截断特殊文件
        if not self._is_special_file(path):
            raise FuseOSError(errno.EPERM)
        
        # 截断文件数据
        if path in self.data:
            self.data[path] = self.data[path][:length]
        else:
            self.data[path] = b''
        
        # 更新文件大小
        self.files[path]['st_size'] = length
        
        return 0
    
    def unlink(self, path):
        # 不允许删除特殊文件
        raise FuseOSError(errno.EPERM)
    
    def utimens(self, path, times=None):
        path = self._full_path(path)
        now = time.time()
        atime, mtime = times if times else (now, now)
        
        if path in self.files:
            self.files[path]['st_atime'] = atime
            self.files[path]['st_mtime'] = mtime
    
    def lock(self, path, fh, cmd, lock):
        # FUSE 锁定操作的简单实现（返回成功）
        return 0
    
    def write(self, path, data, offset, fh):
        path = self._full_path(path)
        
        # 确保路径存在
        if path not in self.files:
            raise FuseOSError(errno.ENOENT)
        
        # 只能写入 input 文件
        if not path.endswith('/input'):
            raise FuseOSError(errno.EPERM)
        
        session_dir = self._get_session_dir(path)
        if not session_dir:
            raise FuseOSError(errno.EPERM)
        
        # 写入 input 文件
        with self.api_lock:
            if offset == 0:  # 覆盖模式
                self.data[path] = data
            else:  # 追加模式
                # 如果需要，扩展数据
                if offset > len(self.data[path]):
                    self.data[path] = self.data[path] + b'\0' * (offset - len(self.data[path]))
                # 写入数据
                self.data[path] = self.data[path][:offset] + data
            
            # 更新文件大小
            new_size = max(offset + len(data), len(self.data[path]))
            self.files[path]['st_size'] = new_size
            self.files[path]['st_mtime'] = time.time()
            
            # 调用 GPT API 并将结果写入 output 文件
            try:
                output_path = os.path.join(session_dir, 'output')
                error_path = os.path.join(session_dir, 'error')
                
                # 清空之前的输出和错误
                self.data[output_path] = b''
                self.data[error_path] = b''
                
                # 调用 API 获取回复
                response = self._call_gpt_api(self.data[path])
                
                # 将响应写入 output 文件
                self.data[output_path] = response
                self.files[output_path]['st_size'] = len(response)
                self.files[output_path]['st_mtime'] = time.time()
            
            except Exception as e:
                # 将错误信息写入 error 文件
                error_msg = f"Error processing request: {str(e)}".encode('utf-8')
                self.data[error_path] = error_msg
                self.files[error_path]['st_size'] = len(error_msg)
                self.files[error_path]['st_mtime'] = time.time()
        
        return len(data)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <mount_point>")
        sys.exit(1)
    
    mount_point = sys.argv[1]
    if not os.path.isdir(mount_point):
        print(f"Error: {mount_point} is not a directory")
        sys.exit(1)
    
    # 检查 API 密钥是否设置
    if not os.getenv('deepseek-api-key'):
        print("Warning: deepseek-api-key environment variable not set. GPT API calls will fail.")
    
    logger.info(f"Starting GPTfs at {mount_point}")
    
    FUSE(GPTfs(), mount_point, nothreads=True, foreground=True)

if __name__ == '__main__':
    main()