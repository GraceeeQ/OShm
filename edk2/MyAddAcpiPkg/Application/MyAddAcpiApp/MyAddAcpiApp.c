#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Protocol/AcpiTable.h>
#include <MyAddAcpiTable.h>
// #include <Guid/Acpi.h>

/**
  计算ACPI表校验和

  @param  Buffer          指向表的指针
  @param  Size            表大小

  @return 表校验和
**/
UINT8
CalculateChecksum (
  IN UINT8  *Buffer,
  IN UINTN  Size
  )
{
  UINT8 Sum;
  UINTN Index;
  
  Sum = 0;
  for (Index = 0; Index < Size; Index++) {
    Sum = (UINT8)(Sum + Buffer[Index]);
  }
  
  return (UINT8)(0xFF - Sum + 1);
}

/**
  应用程序入口点

  @param  ImageHandle     The firmware allocated handle for the EFI image.
  @param  SystemTable     A pointer to the EFI System Table.

  @retval EFI_SUCCESS     The entry point is executed successfully.
  @retval other           Some error occurs when executing this entry point.
**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS              Status;
  EFI_ACPI_TABLE_PROTOCOL *AcpiTable;
  MY_ADD_ACPI_TABLE           *MyTable;
  UINTN                   TableKey;
  
  Print(L"MyAddAcpiApp: Installing custom ACPI table...\n");
  
  // 查找ACPI表协议
  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID**)&AcpiTable
                  );
  if (EFI_ERROR (Status)) {
    Print(L"Error: Could not find ACPI Table Protocol. %r\n", Status);
    return Status;
  }
  
  // 分配表内存
  MyTable = AllocateZeroPool (sizeof(MY_ADD_ACPI_TABLE));
  if (MyTable == NULL) {
    Print(L"Error: Could not allocate memory for ACPI table\n");
    return EFI_OUT_OF_RESOURCES;
  }
  
  // 填充表头
  MyTable->Header.Signature = MY_ADD_ACPI_TABLE_SIGNATURE;
  MyTable->Header.Length = sizeof(MY_ADD_ACPI_TABLE);
  MyTable->Header.Revision = MY_ADD_ACPI_TABLE_REVISION;
  CopyMem (MyTable->Header.OemId, "MYOEM ", 6);
  MyTable->Header.OemTableId = SIGNATURE_64('M','Y','A','C','P','I','T','B'); 
  MyTable->Header.OemRevision = 1;
  MyTable->Header.CreatorId = SIGNATURE_32('M','Y','I','D');
  MyTable->Header.CreatorRevision = 1;
  
  // 填充自定义数据
  MyTable->MyData1 = 0x12345678;
  MyTable->MyData2 = 0x8877665544332211;
  CopyMem (MyTable->MyString, "This is my custom ACPI table for storing information", 50);
  
  // 计算校验和
  MyTable->Header.Checksum = 0;
  MyTable->Header.Checksum = CalculateChecksum ((UINT8*)MyTable, MyTable->Header.Length);
  
  // 安装ACPI表
  TableKey = 0;
  Status = AcpiTable->InstallAcpiTable (
                        AcpiTable,
                        MyTable,
                        MyTable->Header.Length,
                        &TableKey
                        );
  
  if (EFI_ERROR (Status)) {
    Print(L"Error: Failed to install ACPI table. %r\n", Status);
    FreePool (MyTable);
    return Status;
  }
  
  Print(L"Custom ACPI table installed successfully. Table Key: %d\n", TableKey);
  Print(L"Table Signature: %c%c%c%c\n", 
        (CHAR16)((MyTable->Header.Signature >> 0) & 0xFF),
        (CHAR16)((MyTable->Header.Signature >> 8) & 0xFF),
        (CHAR16)((MyTable->Header.Signature >> 16) & 0xFF),
        (CHAR16)((MyTable->Header.Signature >> 24) & 0xFF));
  
  // 释放内存
  FreePool (MyTable);
  Print(L"Done\n");
  return EFI_SUCCESS;
}