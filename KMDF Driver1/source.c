#define NDIS61 1

#include <ntifs.h>
#include <ntddk.h>
#include <wdf.h>

#include <guiddef.h>
#include <initguid.h>
#include <devguid.h>

#pragma warning(push)
#pragma warning(disable: 4201)
#include <fwpsk.h>
#pragma warning(pop)

#include <fwpmk.h>
#include <fwpvi.h>

DEFINE_GUID(WFP_CALLOUT_V4_GUID, 0x4924e857, 0x5ba2, 0x4d21, 0x82, 0xe8, 0x97, 0x24, 0x2, 0xae, 0xcb, 0x71);
DEFINE_GUID(WFP_SUB_LAYER_GUID, 0x9389ac99, 0xb5ee, 0x4b85, 0x9e, 0x3d, 0x48, 0xa8, 0x3c, 0x50, 0x42, 0xe);


PDEVICE_OBJECT DeviceObject = NULL;
HANDLE EngineHandle = NULL;
UINT32 RegCalloutId = 0;
UINT32 AddCalloutId = 0;
UINT64 FilterId = 0;

VOID UnInitWfp()
{
	if (EngineHandle != NULL) {
		if (!FilterId) {
			FwpmFilterDeleteById(EngineHandle, FilterId);
			FwpmSubLayerDeleteByKey(EngineHandle, &WFP_SUB_LAYER_GUID);
		}

		if (AddCalloutId != NULL) {
			FwpmCalloutDeleteById(EngineHandle, AddCalloutId);
		}

		if (RegCalloutId != NULL) {
			FwpsCalloutUnregisterById(RegCalloutId);
		}

		FwpmEngineClose(EngineHandle);
	}
}

VOID Unload(PDRIVER_OBJECT DriverObject)
{
	UnInitWfp();
	IoDeleteDevice(DeviceObject);
	KdPrint(("unload\r\n"));
}

// this function is if any packed is transfered
NTSTATUS NotifyCallback(FWPS_CALLOUT_NOTIFY_TYPE type, const GUID* filterKey, const FWPS_FILTER* filter)
{
	return STATUS_SUCCESS;
}

VOID FlowDeleteCallBack(UINT16 layerId, UINT32 calloutId, UINT64 flowcontext)
{
}

// this function is called in manage if a packed should be allowed or dropped
VOID FilterCallback(const FWPS_INCOMING_VALUES0* values, const FWPS_INCOMING_METADATA_VALUES0* MetaData,
	const void* layerData, const void* context, const FWPS_FILTER* filter, UINT64 flowContext, FWPS_CLASSIFY_OUT* classifyOut)
{
	FWPS_STREAM_CALLOUT_IO_PACKET* packet;
	KdPrint(("data is here\r\n"));

	packet = (FWPS_STREAM_CALLOUT_IO_PACKET*)layerData;

	RtlZeroMemory(classifyOut, sizeof(FWPS_CLASSIFY_OUT));

	packet->streamAction = FWPS_STREAM_ACTION_NONE;
	classifyOut->actionType = FWP_ACTION_PERMIT;

	if (filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT) {
		classifyOut->actionType &= ~FWPS_RIGHT_ACTION_WRITE;
	}
}

NTSTATUS WfpOpenEngine()
{
	return FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, NULL, &EngineHandle);
}

NTSTATUS WfPRegisterCallout()
{
	FWPS_CALLOUT Callout = { 0 };
	Callout.calloutKey = WFP_CALLOUT_V4_GUID;
	Callout.flags = 0;
	Callout.classifyFn = FilterCallback;
	Callout.notifyFn = NotifyCallback;
	Callout.flowDeleteFn = FlowDeleteCallBack;

	return FwpsCalloutRegister(DeviceObject, &Callout, &RegCalloutId);
}

NTSTATUS WfPAddCallout()
{
	FWPM_CALLOUT Callout = { 0 };

	Callout.flags = 0;
	Callout.displayData.name = L"Established Callout Name";
	Callout.displayData.description = L"Established Callout Name";
	Callout.calloutKey = WFP_CALLOUT_V4_GUID;
	Callout.applicableLayer = FWPM_LAYER_STREAM_V4;

	return FwpmCalloutAdd(EngineHandle, &Callout, NULL, &AddCalloutId);
}

NTSTATUS WfPAddSubLayer()
{
	FWPM_SUBLAYER Sublayer = { 0 };

	Sublayer.displayData.name = L"Established Sublayer Name";
	Sublayer.displayData.description = L"Established Sublayer Name";
	Sublayer.subLayerKey = WFP_SUB_LAYER_GUID;
	Sublayer.weight = 65500;

	return FwpmSubLayerAdd(EngineHandle, &Sublayer, NULL);
}

NTSTATUS WfPAddFilter()
{
	FWPM_FILTER Filter = { 0 };
	FWPM_FILTER_CONDITION Condition[1] = { 0 };

	Filter.displayData.name = "Filter Callout Name";
	Filter.displayData.description = "Filter Callout Name";
	Filter.layerKey = FWPM_LAYER_STREAM_V4;
	Filter.subLayerKey = WFP_SUB_LAYER_GUID;
	Filter.weight.type = FWP_EMPTY;
	Filter.numFilterConditions = 1;
	Filter.filterCondition = Condition;
	Filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
	Filter.action.calloutKey = WFP_CALLOUT_V4_GUID;

	// Here we setup a filtering condition, 
	// all packets send or recieved that use port less then 65000 will be blocked
	Condition[0].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
	Condition[0].matchType = FWP_MATCH_LESS_OR_EQUAL;
	Condition[0].conditionValue.type = FWP_UINT16;
	Condition[0].conditionValue.uint16 = 65000;

	return FwpmFilterAdd(EngineHandle, &Filter, NULL, &FilterId);
}

NTSTATUS InitializeWfp()
{
	if (!NT_SUCCESS(WfpOpenEngine())) {
		goto end;
	}

	if (!NT_SUCCESS(WfPRegisterCallout())) {
		goto end;
	}

	if (!NT_SUCCESS(WfPAddCallout())) {
		goto end;
	}

	if (!NT_SUCCESS(WfPAddSubLayer())) {
		goto end;
	}

	if (!NT_SUCCESS(WfPAddFilter())) {
		goto end;
	}

	return STATUS_SUCCESS;
end:
	UnInitWfp();
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	NTSTATUS status;

	DriverObject->DriverUnload = Unload;

	status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = InitializeWfp();
	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(DeviceObject);
	}

	return status;
}

