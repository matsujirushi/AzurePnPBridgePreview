// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "PnpBridgeCommon.h"
#include "PnpAdapterInterface.h"
#include "PnpAdapterManager.h"

extern PPNP_ADAPTER PNP_ADAPTER_MANIFEST[];
extern const int PnpAdapterCount;

PNPBRIDGE_RESULT PnpAdapterManager_ValidatePnpAdapter(PPNP_ADAPTER  pnpAdapter, MAP_HANDLE pnpAdapterMap) {
    bool containsKey = false;
    if (NULL == pnpAdapter->identity) {
        LogError("PnpAdapter's Identity filed is not initialized");
        return PNPBRIDGE_INVALID_ARGS;
    }
    if (MAP_OK != Map_ContainsKey(pnpAdapterMap, pnpAdapter->identity, &containsKey)) {
        LogError("Map_ContainsKey failed");
        return PNPBRIDGE_FAILED;
    }
    if (containsKey) {
        LogError("Found duplicate pnp adapter identity %s", pnpAdapter->identity);
        return PNPBRIDGE_DUPLICATE_ENTRY;
    }
    if (NULL == pnpAdapter->initialize || NULL == pnpAdapter->shutdown || NULL == pnpAdapter->createPnpInterface) {
        LogError("PnpAdapter's callbacks are not initialized");
        return PNPBRIDGE_INVALID_ARGS;
    }

    return PNPBRIDGE_OK;
}

PNPBRIDGE_RESULT PnpAdapterManager_InitializeAdapter(PPNP_ADAPTER_TAG* adapterTag, PPNP_ADAPTER adapter) {
    PNPBRIDGE_RESULT result = PNPBRIDGE_OK;
    PPNP_ADAPTER_TAG adapterT = calloc(1, sizeof(PNP_ADAPTER_TAG));
    if (NULL == adapterT) {

    }

    adapterT->adapter = adapter;
    adapterT->pnpInterfaceList = singlylinkedlist_create();
    adapterT->pnpInterfaceListLock = Lock_Init();

    if (!PNPBRIDGE_SUCCESS(result)) {
        PnpAdapterManager_ReleaseAdapter(adapterT);
    }

    return result;
}

void PnpAdapterManager_ReleaseAdapter(PPNP_ADAPTER_TAG adapterTag) {
    if (NULL != adapterTag) {
        if (NULL != adapterTag->pnpInterfaceList) {
            singlylinkedlist_destroy(adapterTag->pnpInterfaceList);
        }

        if (NULL != adapterTag->pnpInterfaceListLock) {
            Lock_Deinit(adapterTag->pnpInterfaceListLock);
        }

        free(adapterTag);
    }
}

PNPBRIDGE_RESULT PnpAdapterManager_Create(PPNP_ADAPTER_MANAGER* adapterMgr, JSON_Value* config) {
    PNPBRIDGE_RESULT result = PNPBRIDGE_OK;
    PPNP_ADAPTER_MANAGER adapter = NULL;

    if (NULL == adapterMgr) {
        result = PNPBRIDGE_INVALID_ARGS;
        goto exit;
    }

    adapter = (PPNP_ADAPTER_MANAGER) malloc(sizeof(PNP_ADAPTER_MANAGER));
    if (NULL == adapter) {
        result = PNPBRIDGE_INSUFFICIENT_MEMORY;
        goto exit;
    }

    adapter->pnpAdapterMap = Map_Create(NULL);
    if (NULL == adapter->pnpAdapterMap) {
        result = PNPBRIDGE_FAILED;
        goto exit;
    }

    // Create PNP_ADAPTER_HANDLE's
    adapter->pnpAdapters = calloc(PnpAdapterCount, sizeof(PPNP_ADAPTER_TAG));
    if (NULL == adapter->pnpAdapters) {
        result = PNPBRIDGE_FAILED;
        goto exit;
    }

    // Load a list of static modules and build an interface map
    for (int i = 0; i < PnpAdapterCount; i++) {
        PPNP_ADAPTER  pnpAdapter = PNP_ADAPTER_MANIFEST[i];
        adapter->pnpAdapters[i] = calloc(1, sizeof(PNP_ADAPTER_TAG));
        adapter->pnpAdapters[i]->adapter = pnpAdapter;
        adapter->pnpAdapters[i]->pnpInterfaceList = singlylinkedlist_create();
        adapter->pnpAdapters[i]->pnpInterfaceListLock = Lock_Init();

        if (NULL == pnpAdapter->identity) {
            LogError("Invalid Identity specified for a PnpAdapter");
            continue;
        }

        // Validate Pnp Adapter Methods
        result = PnpAdapterManager_ValidatePnpAdapter(pnpAdapter, adapter->pnpAdapterMap);
        if (PNPBRIDGE_OK != result) {
            LogError("PnpAdapter structure is not initialized properly");
            goto exit;
        }

        JSON_Object* initParams = Configuration_GetPnpParameters(config, pnpAdapter->identity);
        const char* initParamstring = NULL;
        if (initParams != NULL) {
            initParamstring = json_serialize_to_string(json_object_get_wrapping_value(initParams));
        }
        result = pnpAdapter->initialize(initParamstring);
        if (PNPBRIDGE_OK != result) {
            LogError("Failed to initialze a PnpAdapter");
            continue;
        }

        Map_Add_Index(adapter->pnpAdapterMap, pnpAdapter->identity, i);
    }

    *adapterMgr = adapter;

exit:
    if (!PNPBRIDGE_SUCCESS(result)) {
        if (NULL != adapter) {
            PnpAdapterManager_Release(adapter);
        }
    }
    return result;
}

void PnpAdapterManager_Release(PPNP_ADAPTER_MANAGER adapter) {
    const char* const* keys;
    const char* const* values;
    size_t count;

    if (NULL != adapter->pnpAdapterMap) {
        // Call shutdown on all interfaces
        if (Map_GetInternals(adapter->pnpAdapterMap, &keys, &values, &count) != MAP_OK) {
            LogError("Map_GetInternals failed to get all pnp adapters");
        }
        else
        {
            for (int i = 0; i < (int)count; i++) {
                int index = values[i][0];
                PPNP_ADAPTER  pnpAdapter = PNP_ADAPTER_MANIFEST[index];
                if (NULL != pnpAdapter->shutdown) {
                    pnpAdapter->shutdown();
                }
            }
        }
    }

    if (NULL != adapter->pnpAdapters) {
        free(adapter->pnpAdapters);;
    }

    Map_Destroy(adapter->pnpAdapterMap);
    free(adapter);
}

PNPBRIDGE_RESULT PnpAdapterManager_SupportsIdentity(PPNP_ADAPTER_MANAGER adapter, JSON_Object* Message, bool* supported, int* key) {
    bool containsMessageKey = false;
    char* interfaceId = NULL;
    JSON_Object* pnpParams = json_object_get_object(Message, PNP_CONFIG_NAME_PNP_PARAMETERS);
    char* getIdentity = (char*) json_object_get_string(pnpParams, PNP_CONFIG_IDENTITY_NAME);
    MAP_RESULT mapResult;

    *supported = false;

    mapResult = Map_ContainsKey(adapter->pnpAdapterMap, getIdentity, &containsMessageKey);
    if (MAP_OK != mapResult || !containsMessageKey) {
        LogError("PnpAdapter %s is not present in AdapterManifest", getIdentity);
        return PNPBRIDGE_FAILED;
    }

    // Get the interface ID
    int index = Map_GetIndexValueFromKey(adapter->pnpAdapterMap, getIdentity);

    *supported = true;
    *key = index;

    return PNPBRIDGE_OK;
}

/*
    Once an interface is registerd with Azure PnpDeviceClient this 
    method will take care of binding it to a module implementing 
    PnP primitives
*/
PNPBRIDGE_RESULT PnpAdapterManager_CreatePnpInterface(PPNP_ADAPTER_MANAGER adapterMgr, PNP_DEVICE_CLIENT_HANDLE pnpDeviceClientHandle,
                  int key, JSON_Object* deviceConfig,
                  PPNPBRIDGE_DEVICE_CHANGE_PAYLOAD DeviceChangePayload) 
{
    // Get the module using the key as index
    PPNP_ADAPTER_TAG  pnpAdapter = adapterMgr->pnpAdapters[key];
    PNP_ADAPTER_CONTEXT_TAG context = { 0 };

    context.adapter = pnpAdapter;
    context.deviceConfig = deviceConfig;

    // Invoke interface binding method
    int ret = pnpAdapter->adapter->createPnpInterface(&context, pnpDeviceClientHandle, DeviceChangePayload);
    if (ret < 0) {
        return PNPBRIDGE_FAILED;
    }

    return PNPBRIDGE_OK;
}

PNPBRIDGE_RESULT PnpAdapterManager_GetAllInterfaces(PPNP_ADAPTER_MANAGER adapterMgr, PNP_INTERFACE_CLIENT_HANDLE** interfaces , int* count) {
    int n = 0;

    // Get the number of created interfaces
    for (int i = 0; i < PnpAdapterCount; i++) {
        PPNP_ADAPTER_TAG  pnpAdapter = adapterMgr->pnpAdapters[i];
        
        SINGLYLINKEDLIST_HANDLE pnpInterfaces = pnpAdapter->pnpInterfaceList;
        LIST_ITEM_HANDLE handle = singlylinkedlist_get_head_item(pnpInterfaces);
        while (NULL != handle) {
            handle = singlylinkedlist_get_next_item(handle);
            n++;
        }
    }

    // create an array of interface handles
    PNP_INTERFACE_CLIENT_HANDLE* pnpClientHandles = NULL;
    {
        pnpClientHandles = calloc(n, sizeof(PNP_INTERFACE_CLIENT_HANDLE));
        int x = 0;
        for (int i = 0; i < PnpAdapterCount; i++) {
            PPNP_ADAPTER_TAG  pnpAdapter = adapterMgr->pnpAdapters[i];

            SINGLYLINKEDLIST_HANDLE pnpInterfaces = pnpAdapter->pnpInterfaceList;
            LIST_ITEM_HANDLE handle = singlylinkedlist_get_head_item(pnpInterfaces);
            while (NULL != handle) {
                PPNPADAPTER_INTERFACE adapterInterface = (PNP_INTERFACE_CLIENT_HANDLE) singlylinkedlist_item_get_value(handle);
                pnpClientHandles[x] = PnpAdapterInterface_GetPnpInterfaceClient(adapterInterface);
                handle = singlylinkedlist_get_next_item(handle);
                x++;
            }
        }
    }

    *count = n;
    *interfaces = pnpClientHandles;

    return 0;
}

bool PnpAdapterManager_IsInterfaceIdPublished(PPNP_ADAPTER_MANAGER adapterMgr, const char* interfaceId) {
    for (int i = 0; i < PnpAdapterCount; i++) {
        PPNP_ADAPTER_TAG  pnpAdapter = adapterMgr->pnpAdapters[i];

        SINGLYLINKEDLIST_HANDLE pnpInterfaces = pnpAdapter->pnpInterfaceList;
        LIST_ITEM_HANDLE handle = singlylinkedlist_get_head_item(pnpInterfaces);
        while (NULL != handle) {
            PPNPADAPTER_INTERFACE adapterInterface = (PNP_INTERFACE_CLIENT_HANDLE)singlylinkedlist_item_get_value(handle);
            if (stricmp(adapterInterface->interfaceId, interfaceId)) {
                return true;
            }
            handle = singlylinkedlist_get_next_item(handle);
        }
    }

    return false;
}

PNPBRIDGE_RESULT PnpAdapterManager_ReleasePnpInterface(PPNP_ADAPTER_MANAGER adapter, PNPADAPTER_INTERFACE_HANDLE interfaceClient) {
    if (NULL == interfaceClient) {
        return PNPBRIDGE_INVALID_ARGS;
    }

    PPNPADAPTER_INTERFACE pnpInterface = (PPNPADAPTER_INTERFACE)interfaceClient;

    // Get the module index
    PPNP_ADAPTER  pnpAdapter = PNP_ADAPTER_MANIFEST[pnpInterface->key];
    //pnpAdapter->releaseInterface(interfaceClient);

    return PNPBRIDGE_OK;
}

void PnpAdapterManager_AddInterface(PPNP_ADAPTER_TAG adapter, PNPADAPTER_INTERFACE_HANDLE pnpAdapterInterface) {
    LIST_ITEM_HANDLE handle = NULL;
    Lock(adapter->pnpInterfaceListLock);
    handle = singlylinkedlist_add(adapter->pnpInterfaceList, pnpAdapterInterface);
    Unlock(adapter->pnpInterfaceListLock);

    if (NULL != handle) {
        PPNPADAPTER_INTERFACE interface = (PPNPADAPTER_INTERFACE)pnpAdapterInterface;
        interface->adapterEntry = handle;
    }
}

void PnpAdapterManager_RemoveInterface(PPNP_ADAPTER_TAG adapter, PNPADAPTER_INTERFACE_HANDLE pnpAdapterInterface) {
    PPNPADAPTER_INTERFACE interface = (PPNPADAPTER_INTERFACE)pnpAdapterInterface;
    if (NULL == interface->adapterEntry) {
        return;
    }

    Lock(adapter->pnpInterfaceListLock);
    singlylinkedlist_remove(adapter->pnpInterfaceList, interface->adapterEntry);
    Unlock(adapter->pnpInterfaceListLock);
}