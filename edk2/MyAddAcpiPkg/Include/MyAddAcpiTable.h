#ifndef MY_ADD_ACPI_TABLE_H_
#define MY_ADD_ACPI_TABLE_H_

#include <IndustryStandard/Acpi.h>

#define MY_ADD_ACPI_TABLE_SIGNATURE  SIGNATURE_32('M','Y','T','B')
#define MY_ADD_ACPI_TABLE_REVISION   1

#pragma pack(1)

typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  // 在这里定义您的自定义数据字段
  UINT32 MyData1;
  UINT64 MyData2;
  CHAR8  MyString[64];
} MY_ADD_ACPI_TABLE;

#pragma pack()

extern EFI_GUID gMyAddAcpiTableGuid;

#endif // MY_ADD_ACPI_TABLE_H_