#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/Acpi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
VOID PrintAllACPITables();
VOID PrintHeader(EFI_ACPI_DESCRIPTION_HEADER *head, BOOLEAN isRoot);
VOID ChangeACPITable(UINTN index, EFI_ACPI_DESCRIPTION_HEADER *newTable);
EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status = EFI_SUCCESS;
    PrintAllACPITables();
    EFI_ACPI_DESCRIPTION_HEADER emptyTable = {0};
    ChangeACPITable(1, &emptyTable);
    ChangeACPITable(2, &emptyTable);
    PrintAllACPITables();
    Print(L"BYE BYE ~~~\n");
    return Status;
}
VOID PrintAllACPITables()
{
    EFI_CONFIGURATION_TABLE *configTable = NULL;
    configTable = gST->ConfigurationTable;
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++)
    {
        if (CompareGuid(&configTable[i].VendorGuid, &gEfiAcpiTableGuid) || CompareGuid(&configTable[i].VendorGuid, &gEfiAcpi20TableGuid))
        {
            Print(L"Found ACPI table\n");
            EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *Root = configTable[i].VendorTable;
            Print(L"================ RSDP TABLE ================\n");
            Print(L"Signature: ");
            for (int i = 0; i < 8; i++)
            {
                Print(L"%c", Root->Signature >> (i * 8) & 0xff);
            }
            Print(L"\n");
            Print(L"Length: %d\n", Root->Length);
            Print(L"OEM ID: ");
            for (int i = 0; i < 6; i++)
            {
                Print(L"%c", Root->OemId[i]);
            }
            Print(L"\n");
            Print(L"Checksum: 0x%x\n", Root->Checksum);
            EFI_ACPI_DESCRIPTION_HEADER *Xsdt = (VOID *)Root->XsdtAddress;
            PrintHeader(Xsdt, FALSE);
            UINT64 *Entry = (UINT64 *)(Xsdt + 1);
            UINTN EntryCount = (Xsdt->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER)) / sizeof(UINT64);
            for (UINTN i = 0; i < EntryCount; i++)
            {
                PrintHeader((EFI_ACPI_DESCRIPTION_HEADER *)Entry[i], FALSE);
            }
            break;
        }
    }
}
VOID PrintHeader(EFI_ACPI_DESCRIPTION_HEADER *head, BOOLEAN isRoot)
{
    if (!isRoot)
    {
        Print(L"================ ");
        for (int i = 0; i < 4; i++)
        {
            Print(L"%c", head->Signature >> (i * 8) & 0xff);
        }
        Print(L" TABLE ================\n");
    }

    Print(L"Physics Address: 0x%p\n", head);
    Print(L"Length: %d\n", head->Length);
    Print(L"OEM ID: ");
    for (int i = 0; i < 6; i++)
    {
        Print(L"%c", head->OemId[i]);
    }
    Print(L"\n");
    Print(L"Checksum: 0x%x\n", head->Checksum);
    // if it is facp:
    if (head->Signature == 0x50434146)
    {
        EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE *FACP = (EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE *)head;
        if (FACP->FirmwareCtrl == 0)
        {
            Print(L"================Firmware Control: Not Present================\n");
        }
        else
        {
            PrintHeader((EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)(FACP->FirmwareCtrl), FALSE);
        }
        if (FACP->Dsdt == 0)
        {
            Print(L"================DSDT: Not Present================\n");
        }
        else
        {
            PrintHeader((EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)(FACP->Dsdt), FALSE);
        }
        if (FACP->XFirmwareCtrl == 0)
        {
            Print(L"================X Firmware Control: Not Present================\n");
        }
        else
        {
            PrintHeader((EFI_ACPI_DESCRIPTION_HEADER *)(FACP->XFirmwareCtrl), FALSE);
        }
        if (FACP->XDsdt == 0)
        {
            Print(L"================X DSDT: Not Present================\n");
        }
        else
        {
            PrintHeader((EFI_ACPI_DESCRIPTION_HEADER *)(FACP->XDsdt), FALSE);
        }
    }
}
VOID ChangeACPITable(UINTN index, EFI_ACPI_DESCRIPTION_HEADER *newTable)
{
    Print(L"Changing ACPI table %d\n", index);
    EFI_CONFIGURATION_TABLE *configTable = NULL;
    configTable = gST->ConfigurationTable;
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++)
    {
        if (CompareGuid(&configTable[i].VendorGuid, &gEfiAcpiTableGuid) || CompareGuid(&configTable[i].VendorGuid, &gEfiAcpi20TableGuid))
        {
            Print(L"Found ACPI table\n");
            EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *Root = configTable[i].VendorTable;
            EFI_ACPI_DESCRIPTION_HEADER *Xsdt = (VOID *)Root->XsdtAddress;
            UINT64 *Entry = (UINT64 *)(Xsdt + 1);
            EFI_ACPI_DESCRIPTION_HEADER *table = (EFI_ACPI_DESCRIPTION_HEADER *)Entry[index];
            //print name
            Print(L"changing table %d ", index);
            for (int i = 0; i < 4; i++)
            {
                Print(L"%c", table->Signature >> (i * 8) & 0xff);
            }
            Print(L"\n");

            CopyMem(table, newTable, table->Length);
            // Entry[index] = (UINT64)newTable;
            // EFI_ACPI_DESCRIPTION_HEADER *table = (EFI_ACPI_DESCRIPTION_HEADER *)Entry[index];
            table->Checksum = 0;
            table->Checksum = CalculateCheckSum8((UINT8 *)Entry[index], table->Length);
            break;
        }
    }
}
