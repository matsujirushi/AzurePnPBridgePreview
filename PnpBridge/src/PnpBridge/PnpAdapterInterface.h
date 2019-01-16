#pragma once

typedef struct _PNPADAPTER_INTERFACE {
	PNP_INTERFACE_CLIENT_HANDLE Interface;
	void* Context;
	int key;
} PNPADAPTER_INTERFACE, *PPNPADAPTER_INTERFACE;

typedef void* PNPADAPTER_INTERFACE_HANDLE;

typedef PNPBRIDGE_RESULT (*PNP_INTERFACE_INITIALIZE) (JSON_Object* args);

typedef int(*PNP_INTERFACE_SHUTDOWN)();

// Need to pass arguments (filter)
typedef int(*BIND_PNP_INTERFACE)(PNPADAPTER_INTERFACE_HANDLE Interface, PPNPBRIDGE_DEVICE_CHANGE_PAYLOAD args);

typedef int(*RELEASE_PNP_INTERFACE)(PNPADAPTER_INTERFACE_HANDLE Interface);

typedef struct PNP_INTERFACE_MODULE {
    const char* Identity;
    PNP_INTERFACE_INITIALIZE Initialize;
	BIND_PNP_INTERFACE BindPnpInterface;
	RELEASE_PNP_INTERFACE ReleaseInterface;
    PNP_INTERFACE_SHUTDOWN Shutdown;
} PNP_INTERFACE_MODULE, *PPNP_INTERFACE_MODULE;


/* PnpAdapter API's*/
PNP_INTERFACE_CLIENT_HANDLE PnpAdapter_GetPnpInterface(PNPADAPTER_INTERFACE_HANDLE PnpInterfaceClient);

PNPBRIDGE_RESULT PnpAdapter_SetContext(PNPADAPTER_INTERFACE_HANDLE PnpInterfaceClient, void* Context);
void* PnpAdapter_GetContext(PNPADAPTER_INTERFACE_HANDLE PnpInterfaceClient);