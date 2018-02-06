// FPAuth.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

using namespace TpmCpp;

#define NV_FPBASE_INDEX (0x00008000)
#define FP_TEMPLATE_SIZE (498)
#define FP_SLOTS_MAX (200)
#define FP_SLOTS (2)
#define FP_AUTHORIZE_INDEX (NV_FPBASE_INDEX + FP_SLOTS_MAX)
#define FP_DISPLAY_INDEX (FP_AUTHORIZE_INDEX + 1)
#define FP_DISPLAY_MAX_TEXT (256)

#define FP_SLOT_INITIALIZE_TEMPLATE (0x00)
#define FP_SLOT_DELETE_ALL_TEMPLATE (0x01)
#define FP_SLOT_DELETE_TEMPLATE (0x02)
#define FP_SLOT_ENROLL_TEMPLATE (0x03)
#define FP_AUTHORIZE_INITIALIZE (0x00)
#define FP_AUTHORIZE_VERIFY (0x01)
#define FP_AUTHORIZE_TIMEOUT (0x02)

vector<BYTE> NullVec;

int main()
{
    Tpm2 tpm;
    TpmTcpDevice device;
    TPM_HANDLE nvHandle;
    ByteVec fpTemplate[FP_SLOTS];
    std::string err;
    ByteVec fpId;

    // Connect the Tpm2 device to a simulator running on the same machine
    if (!device.Connect("127.0.0.1", 2321))
    {
        cerr << "Could not connect to the TPM device";
        return 0;
    }
    tpm._SetDevice(device);

    // Power-cycle the simulator
    device.PowerOff();
    device.PowerOn();

    // and startup the TPM
    tpm.Startup(TPM_SU::CLEAR);

    tpm.DictionaryAttackLockReset(tpm._AdminLockout);

    //for (unsigned int n = 0; n < FP_SLOTS; n++)
    //{
    //    nvHandle = TPM_HANDLE::NVHandle(NV_FPBASE_INDEX + n);
    //    tpm._AllowErrors().NV_UndefineSpace(tpm._AdminPlatform, nvHandle);
    //    err = tpm._GetLastErrorAsString();
    //}
    //tpm._AllowErrors().NV_UndefineSpace(tpm._AdminPlatform, TPM_HANDLE::NVHandle(FP_AUTHORIZE_INDEX));
    //err = tpm._GetLastErrorAsString();
    //tpm._AllowErrors().Clear(tpm._AdminLockout);
    //err = tpm._GetLastErrorAsString();

    // Setup the NV indices
    for (unsigned int n = 0; n < FP_SLOTS; n++)
    {
        nvHandle = TPM_HANDLE::NVHandle(NV_FPBASE_INDEX + n);
        NV_ReadPublicResponse nvPub = tpm._AllowErrors().NV_ReadPublic(nvHandle);
        err = tpm._GetLastErrorAsString();
        if (tpm._GetLastError() != TPM_RC::SUCCESS)
        {
            TPMS_NV_PUBLIC nvTemplate(
                nvHandle,           // Index handle
                TPM_ALG_ID::SHA256, // Name-alg
                TPMA_NV::AUTHREAD | // Attributes
                TPMA_NV::AUTHWRITE |
                TPMA_NV::PLATFORMCREATE,
                NullVec,            // Policy
                FP_TEMPLATE_SIZE);  // Size in bytes
            tpm.NV_DefineSpace(tpm._AdminPlatform, NullVec, nvTemplate);
            do
            {
                // Initialize space - this will delete the template from the FPR
                tpm._AllowErrors().NV_Write(nvHandle, nvHandle, vector<BYTE>{FP_SLOT_INITIALIZE_TEMPLATE}, 0);
            } while (tpm._GetLastError() == TPM_RC::RETRY);
            if (tpm._GetLastError() != TPM_RC::SUCCESS)
            {
                throw;
            }
        }
    }
    {
        nvHandle = TPM_HANDLE::NVHandle(FP_AUTHORIZE_INDEX);
        NV_ReadPublicResponse nvPub = tpm._AllowErrors().NV_ReadPublic(nvHandle);
        if (tpm._GetLastError() != TPM_RC::SUCCESS)
        {
            TPMS_NV_PUBLIC nvTemplate(
                nvHandle,           // Index handle
                TPM_ALG_ID::SHA256, // Name-alg
                TPMA_NV::AUTHREAD | // Attributes
                TPMA_NV::AUTHWRITE |
                TPMA_NV::PLATFORMCREATE,
                NullVec,            // Policy
                sizeof(unsigned int));  // Size in bytes
            tpm.NV_DefineSpace(tpm._AdminPlatform, NullVec, nvTemplate);
            do
            {
                // Initialize space - this will delete the template from the FPR
                vector<BYTE> data(sizeof(unsigned int), 0x00);
                data[0] = FP_AUTHORIZE_INITIALIZE;
                tpm._AllowErrors().NV_Write(nvHandle, nvHandle, data, 0);
            } while (tpm._GetLastError() == TPM_RC::RETRY);
        }
    }
    {
        nvHandle = TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX);
        NV_ReadPublicResponse nvPub = tpm._AllowErrors().NV_ReadPublic(nvHandle);
        if (tpm._GetLastError() != TPM_RC::SUCCESS)
        {
            TPMS_NV_PUBLIC nvTemplate(
                nvHandle,           // Index handle
                TPM_ALG_ID::SHA256, // Name-alg
                TPMA_NV::AUTHREAD | // Attributes
                TPMA_NV::AUTHWRITE |
                TPMA_NV::PLATFORMCREATE,
                NullVec,            // Policy
                FP_DISPLAY_MAX_TEXT);  // Size in bytes
            tpm.NV_DefineSpace(tpm._AdminPlatform, NullVec, nvTemplate);
            do
            {
                // Initialize space - this will delete the template from the FPR
                vector<BYTE> data(FP_DISPLAY_MAX_TEXT, 0x00);
                tpm._AllowErrors().NV_Write(nvHandle, nvHandle, data, 0);
            } while (tpm._GetLastError() == TPM_RC::RETRY);
        }
    }

    // Make sure NV is up and running
    do
    {
        vector<BYTE> data(sizeof(unsigned int), 0x00);
        data[0] = FP_AUTHORIZE_INITIALIZE;
        tpm._AllowErrors().NV_Write(nvHandle, nvHandle, data, 0);
    } while (tpm._GetLastError() == TPM_RC::RETRY);

    // Delete all templates
    printf("Deleting all templates...");
    tpm.NV_Write(TPM_HANDLE::NVHandle(NV_FPBASE_INDEX), TPM_HANDLE::NVHandle(NV_FPBASE_INDEX), vector<BYTE>{FP_SLOT_DELETE_ALL_TEMPLATE}, 0);
    printf("Done.\n");

    // Enroll a fingers
    for (unsigned int n = 0; n < FP_SLOTS; n++)
    {
        printf("Enrolling template %d...\n", n);
        nvHandle = TPM_HANDLE::NVHandle(NV_FPBASE_INDEX + n);
        fpTemplate[0] = tpm._AllowErrors().NV_Read(nvHandle, nvHandle, FP_TEMPLATE_SIZE, 0);
        if (tpm._GetLastError() == TPM_RC::FAILURE)
        {
            const char msg1[] = "Enroll Finger";
            tpm.NV_Write(TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), vector<BYTE>(msg1, msg1 + sizeof(msg1)), 0);
            do
            {
                tpm._AllowErrors().NV_Write(nvHandle, nvHandle, vector<BYTE>{FP_SLOT_ENROLL_TEMPLATE}, 0);
                if (tpm._GetLastError() == TPM_RC::FAILURE)
                {
                    printf("Enroll failed, retry.\n");
                }
            } while (tpm._GetLastError() == TPM_RC::FAILURE);
            if (tpm._GetLastError() != TPM_RC::SUCCESS)
            {
                throw;
            }
            fpTemplate[n] = tpm.NV_Read(nvHandle, nvHandle, FP_TEMPLATE_SIZE, 0);
        }
        else if (tpm._GetLastError() != TPM_RC::SUCCESS)
        {
            throw;
        }
        tpm.NV_Write(TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), vector<BYTE>(0), 0);
        Sleep(2000);
    }

    // Identify FP
    printf("Authorization 1...\n");
    const char msg3[] = "Authorize the transaction.";
    tpm.NV_Write(TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), vector<BYTE>(msg3, msg3 + sizeof(msg3)), 0);
    nvHandle = TPM_HANDLE::NVHandle(FP_AUTHORIZE_INDEX);
    do
    {
        fpId = tpm._AllowErrors().NV_Read(nvHandle, nvHandle, sizeof(unsigned int), 0);
        if (tpm._GetLastError() == TPM_RC::FAILURE)
        {
            printf("Authorize operation failed, retry.\n");
        }
    } while (tpm._GetLastError() == TPM_RC::FAILURE);
    if (tpm._GetLastError() == TPM_RC::CANCELED)
    {
        printf("No finger provided in time for transaction 1.\n");
    }
    else if (tpm._GetLastError() != TPM_RC::SUCCESS)
    {
        throw;
    }
    else if (fpId.size() == sizeof(int))
    {
        if (*((int*)fpId.data()) == -1)
        {
            printf("Unidentified finger for transaction 1.\n");
        }
        else
        {
            printf("Identified finger with the template in slot %d for transaction 1.\n", *((unsigned int*)fpId.data()));
        }
    }
    else
    {
        printf("Invalid data returned for transaction 1.\n");
    }
    tpm.NV_Write(TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), vector<BYTE>(0), 0);
    Sleep(2000);

    // Swap the templates in the reader
    printf("Swapping templates...\n");
    for (unsigned int n = 0; n < 2; n++)
    {
        nvHandle = TPM_HANDLE::NVHandle(NV_FPBASE_INDEX + n);
        tpm.NV_Write(nvHandle, nvHandle, vector<BYTE>{FP_SLOT_DELETE_TEMPLATE}, 0);
    }
    for (unsigned int n = 0; n < 2; n++)
    {
        nvHandle = TPM_HANDLE::NVHandle(NV_FPBASE_INDEX + n);
        tpm.NV_Write(nvHandle, nvHandle, fpTemplate[1 - n], 0);
    }
    printf("Done.\n");

    // Identify and verify FP
    printf("Authorization 2...\n");
    tpm.NV_Write(TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), vector<BYTE>(msg3, msg3 + sizeof(msg3)), 0);
    nvHandle = TPM_HANDLE::NVHandle(FP_AUTHORIZE_INDEX);
    do
    {
        fpId = tpm._AllowErrors().NV_Read(nvHandle, nvHandle, sizeof(unsigned int), 0);
        if (tpm._GetLastError() == TPM_RC::FAILURE)
        {
            printf("Authorize operation failed, retry.\n");
        }
    } while (tpm._GetLastError() == TPM_RC::FAILURE);
    if (tpm._GetLastError() == TPM_RC::CANCELED)
    {
        printf("No finger provided in time for transaction 1.\n");
    }
    else if (tpm._GetLastError() != TPM_RC::SUCCESS)
    {
        throw;
    }
    else if (fpId.size() == sizeof(int))
    {
        if (*((int*)fpId.data()) == -1)
        {
            printf("Unidentified finger for transaction 1.\n");
        }
        else
        {
            printf("Identified finger with the template in slot %d for transaction 1.\n", *((unsigned int*)fpId.data()));
        }
    }
    else
    {
        printf("Invalid data returned for transaction 1.\n");
    }
    tpm.NV_Write(TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), TPM_HANDLE::NVHandle(FP_DISPLAY_INDEX), vector<BYTE>(0), 0);

    tpm.Shutdown(TPM_SU::CLEAR);
    Sleep(10000);

    return 0;
}

