#include "audio_input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cwctype>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef __linux__
#include <alsa/asoundlib.h>
#include <cstdio>

static void suppress_alsa_errors(const char* file, int line, const char* function, int err, const char* fmt, ...) {
    (void)file; (void)line; (void)function; (void)err; (void)fmt;
}

class ALSAErrorSuppressor {
public:
    ALSAErrorSuppressor() {
        snd_lib_error_set_handler(suppress_alsa_errors);
    }

    ~ALSAErrorSuppressor() {
        snd_lib_error_set_handler(nullptr);
    }
};
#endif

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pa_mac_core.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define INITGUID
#include <windows.h>
#include <devpkey.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <pa_win_wasapi.h>
#include <propidl.h>
#include <setupapi.h>
#endif

namespace {

float smoothedLevel(const float current, const float target) {
	return std::max(target, current * 0.84f);
}

#ifdef __APPLE__
constexpr UInt32 makeFourCC(const char a, const char b, const char c, const char d) {
	return (static_cast<UInt32>(static_cast<unsigned char>(a)) << 24U) |
		   (static_cast<UInt32>(static_cast<unsigned char>(b)) << 16U) |
		   (static_cast<UInt32>(static_cast<unsigned char>(c)) << 8U) |
		   static_cast<UInt32>(static_cast<unsigned char>(d));
}

bool getAudioObjectProperty(const AudioObjectID objectId,
							const AudioObjectPropertySelector selector,
							const AudioObjectPropertyScope scope,
							const AudioObjectPropertyElement element,
							UInt32& dataSize,
							void* data) {
	AudioObjectPropertyAddress address{selector, scope, element};
	return AudioObjectGetPropertyData(objectId, &address, 0, nullptr, &dataSize, data) == noErr;
}

std::vector<AudioDeviceID> coreAudioDeviceIds() {
	AudioObjectPropertyAddress address{
		kAudioHardwarePropertyDevices,
		kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain
	};
	UInt32 dataSize = 0;
	if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &dataSize) != noErr ||
		dataSize == 0) {
		return {};
	}

	std::vector<AudioDeviceID> deviceIds(dataSize / sizeof(AudioDeviceID));
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &dataSize, deviceIds.data()) != noErr) {
		return {};
	}
	return deviceIds;
}

std::string coreAudioDeviceName(const AudioDeviceID deviceId) {
	CFStringRef nameRef = nullptr;
	UInt32 dataSize = sizeof(nameRef);
	if (!getAudioObjectProperty(deviceId,
								kAudioDevicePropertyDeviceNameCFString,
								kAudioObjectPropertyScopeGlobal,
								kAudioObjectPropertyElementMain,
								dataSize,
								&nameRef) ||
		nameRef == nullptr) {
		return {};
	}

	char name[256]{};
	const bool copied = CFStringGetCString(nameRef, name, sizeof(name), kCFStringEncodingUTF8);
	CFRelease(nameRef);
	return copied ? std::string(name) : std::string();
}

int coreAudioChannelCount(const AudioDeviceID deviceId, const AudioObjectPropertyScope scope) {
	AudioObjectPropertyAddress address{
		kAudioDevicePropertyStreamConfiguration,
		scope,
		kAudioObjectPropertyElementMain
	};
	UInt32 dataSize = 0;
	if (AudioObjectGetPropertyDataSize(deviceId, &address, 0, nullptr, &dataSize) != noErr ||
		dataSize == 0) {
		return -1;
	}

	std::vector<std::byte> storage(dataSize);
	auto* bufferList = reinterpret_cast<AudioBufferList*>(storage.data());
	if (AudioObjectGetPropertyData(deviceId, &address, 0, nullptr, &dataSize, bufferList) != noErr) {
		return -1;
	}

	UInt32 channelCount = 0;
	for (UInt32 i = 0; i < bufferList->mNumberBuffers; ++i) {
		channelCount += bufferList->mBuffers[i].mNumberChannels;
	}
	return static_cast<int>(channelCount);
}

bool hasCoreAudioDeviceInfo(const AudioDeviceID deviceId) {
	return !coreAudioDeviceName(deviceId).empty() &&
		   coreAudioChannelCount(deviceId, kAudioObjectPropertyScopeInput) >= 0 &&
		   coreAudioChannelCount(deviceId, kAudioObjectPropertyScopeOutput) >= 0;
}

std::optional<int> coreAudioHostApiDeviceIndex(const PaDeviceIndex paIndex, const PaDeviceInfo& paDeviceInfo) {
	const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(paDeviceInfo.hostApi);
	if (!hostApiInfo || hostApiInfo->type != paCoreAudio) {
		return std::nullopt;
	}

	for (int i = 0; i < hostApiInfo->deviceCount; ++i) {
		if (Pa_HostApiDeviceIndexToDeviceIndex(paDeviceInfo.hostApi, i) == paIndex) {
			return i;
		}
	}
	return std::nullopt;
}

std::optional<AudioDeviceID> coreAudioDeviceIdForPortAudioDevice(const PaDeviceIndex paIndex,
																 const PaDeviceInfo& paDeviceInfo) {
	const std::optional<int> hostApiDeviceIndex = coreAudioHostApiDeviceIndex(paIndex, paDeviceInfo);
	if (!hostApiDeviceIndex) {
		return std::nullopt;
	}

	std::vector<AudioDeviceID> deviceIds = coreAudioDeviceIds();
	std::vector<AudioDeviceID> portAudioDeviceOrder;
	portAudioDeviceOrder.reserve(deviceIds.size());
	for (const AudioDeviceID deviceId : deviceIds) {
		if (hasCoreAudioDeviceInfo(deviceId)) {
			portAudioDeviceOrder.push_back(deviceId);
		}
	}

	if (*hostApiDeviceIndex >= 0 && static_cast<size_t>(*hostApiDeviceIndex) < portAudioDeviceOrder.size()) {
		const AudioDeviceID orderedDeviceId = portAudioDeviceOrder[static_cast<size_t>(*hostApiDeviceIndex)];
		if (coreAudioDeviceName(orderedDeviceId) == paDeviceInfo.name) {
			return orderedDeviceId;
		}
	}

	for (const AudioDeviceID deviceId : portAudioDeviceOrder) {
		if (coreAudioDeviceName(deviceId) == paDeviceInfo.name &&
			coreAudioChannelCount(deviceId, kAudioObjectPropertyScopeInput) == paDeviceInfo.maxInputChannels &&
			coreAudioChannelCount(deviceId, kAudioObjectPropertyScopeOutput) == paDeviceInfo.maxOutputChannels) {
			return deviceId;
		}
	}

	return std::nullopt;
}

bool isContinuityCaptureTransport(const UInt32 transportType) {
	return transportType == makeFourCC('c', 'c', 'w', 'd') ||
		   transportType == makeFourCC('c', 'c', 'w', 'l') ||
		   transportType == makeFourCC('c', 'c', 'a', 'p');
}

bool isBluetoothAudioTransport(const UInt32 transportType) {
	return transportType == makeFourCC('b', 'l', 'u', 'e') ||
		   transportType == makeFourCC('b', 'l', 'e', 'a');
}

bool looksLikeContinuityDeviceName(const std::string_view name) {
	std::string lowerName;
	lowerName.reserve(name.size());
	for (const char ch : name) {
		lowerName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	}
	return lowerName.find("iphone") != std::string::npos ||
		   lowerName.find("ipad") != std::string::npos ||
		   lowerName.find("continuity") != std::string::npos;
}

bool looksLikeBuiltInMacBookMicrophone(const std::string_view name) {
	std::string lowerName;
	lowerName.reserve(name.size());
	for (const char ch : name) {
		lowerName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	}
	return (lowerName.find("macbook") != std::string::npos &&
			lowerName.find("microphone") != std::string::npos) ||
		   lowerName.find("built-in microphone") != std::string::npos;
}

bool isCoreAudioInputDevice(const PaDeviceInfo& paDeviceInfo) {
	const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(paDeviceInfo.hostApi);
	return hostApiInfo != nullptr && hostApiInfo->type == paCoreAudio;
}

bool shouldUseMonoCoreAudioInput(const PaDeviceInfo& paDeviceInfo) {
	return isCoreAudioInputDevice(paDeviceInfo) &&
		   looksLikeBuiltInMacBookMicrophone(paDeviceInfo.name != nullptr ? paDeviceInfo.name : "");
}

std::optional<SInt32> preferredCoreAudioInputChannel(const PaDeviceIndex paIndex,
													 const PaDeviceInfo& paDeviceInfo) {
	const std::optional<AudioDeviceID> deviceId = coreAudioDeviceIdForPortAudioDevice(paIndex, paDeviceInfo);
	if (!deviceId) {
		return std::nullopt;
	}

	std::array<UInt32, 2> preferredChannels{};
	UInt32 dataSize = static_cast<UInt32>(preferredChannels.size() * sizeof(UInt32));
	if (!getAudioObjectProperty(*deviceId,
								kAudioDevicePropertyPreferredChannelsForStereo,
								kAudioObjectPropertyScopeInput,
								kAudioObjectPropertyElementMain,
								dataSize,
								preferredChannels.data()) ||
		dataSize < sizeof(UInt32) ||
		preferredChannels[0] == 0) {
		return std::nullopt;
	}

	return static_cast<SInt32>(preferredChannels[0] - 1);
}

bool shouldProtectPassiveLevelMonitoring(const PaDeviceIndex paIndex, const PaDeviceInfo& paDeviceInfo) {
	const std::optional<AudioDeviceID> deviceId = coreAudioDeviceIdForPortAudioDevice(paIndex, paDeviceInfo);
	if (deviceId) {
		UInt32 transportType = 0;
		UInt32 dataSize = sizeof(transportType);
		if (getAudioObjectProperty(*deviceId,
								   kAudioDevicePropertyTransportType,
								   kAudioObjectPropertyScopeGlobal,
								   kAudioObjectPropertyElementMain,
								   dataSize,
								   &transportType)) {
			return isContinuityCaptureTransport(transportType) ||
				   isBluetoothAudioTransport(transportType);
		}
	}

	return looksLikeContinuityDeviceName(paDeviceInfo.name != nullptr ? paDeviceInfo.name : "");
}
#elif !defined(_WIN32)
bool shouldProtectPassiveLevelMonitoring(const PaDeviceIndex, const PaDeviceInfo&) {
	return false;
}

bool shouldUseMonoCoreAudioInput(const PaDeviceInfo&) {
	return false;
}
#endif

#ifdef _WIN32
std::wstring toLowerWide(std::wstring value) {
	std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
		return static_cast<wchar_t>(std::towlower(ch));
	});
	return value;
}

bool wideTextContainsBluetoothHint(const std::wstring& value) {
	const std::wstring lower = toLowerWide(value);
	return lower.find(L"bluetooth") != std::wstring::npos ||
		   lower.find(L"hands-free") != std::wstring::npos ||
		   lower.find(L"hands free") != std::wstring::npos;
}

bool wideTextContainsBluetoothIdHint(const std::wstring& value) {
	return wideTextContainsBluetoothHint(value) ||
		   toLowerWide(value).find(L"bth") != std::wstring::npos;
}

bool textContainsBluetoothHint(const std::string_view value) {
	std::string lower;
	lower.reserve(value.size());
	for (const char ch : value) {
		lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	}
	return lower.find("bluetooth") != std::string::npos ||
		   lower.find("hands-free") != std::string::npos ||
		   lower.find("hands free") != std::string::npos;
}

bool propVariantToGuid(const PROPVARIANT& value, GUID& guid) {
	if (value.vt == VT_CLSID && value.puuid != nullptr) {
		guid = *value.puuid;
		return true;
	}
	if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
		return CLSIDFromString(value.pwszVal, &guid) == S_OK;
	}
	return false;
}

std::optional<GUID> devicePropertyGuid(HDEVINFO deviceInfoSet,
									   SP_DEVINFO_DATA& deviceInfoData,
									   const DEVPROPKEY& key) {
	DEVPROPTYPE propertyType = 0;
	GUID value{};
	DWORD requiredSize = 0;
	if (SetupDiGetDevicePropertyW(deviceInfoSet,
								  &deviceInfoData,
								  &key,
								  &propertyType,
								  reinterpret_cast<PBYTE>(&value),
								  sizeof(value),
								  &requiredSize,
								  0) &&
		propertyType == DEVPROP_TYPE_GUID) {
		return value;
	}
	return std::nullopt;
}

std::wstring devicePropertyString(HDEVINFO deviceInfoSet,
								  SP_DEVINFO_DATA& deviceInfoData,
								  const DEVPROPKEY& key) {
	DEVPROPTYPE propertyType = 0;
	DWORD requiredSize = 0;
	if (SetupDiGetDevicePropertyW(deviceInfoSet,
								  &deviceInfoData,
								  &key,
								  &propertyType,
								  nullptr,
								  0,
								  &requiredSize,
								  0) ||
		GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
		requiredSize == 0) {
		return {};
	}

	std::vector<BYTE> buffer(requiredSize);
	if (!SetupDiGetDevicePropertyW(deviceInfoSet,
								   &deviceInfoData,
								   &key,
								   &propertyType,
								   buffer.data(),
								   static_cast<DWORD>(buffer.size()),
								   nullptr,
								   0) ||
		propertyType != DEVPROP_TYPE_STRING) {
		return {};
	}

	return reinterpret_cast<const wchar_t*>(buffer.data());
}

std::wstring deviceInstanceId(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData) {
	DWORD requiredSize = 0;
	SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData, nullptr, 0, &requiredSize);
	if (requiredSize == 0) {
		return {};
	}

	std::vector<wchar_t> buffer(requiredSize);
	if (!SetupDiGetDeviceInstanceIdW(deviceInfoSet,
									 &deviceInfoData,
									 buffer.data(),
									 static_cast<DWORD>(buffer.size()),
									 nullptr)) {
		return {};
	}
	return buffer.data();
}

bool isBluetoothEnumeratorName(const std::wstring& value) {
	const std::wstring lower = toLowerWide(value);
	return lower == L"bthenum" ||
		   lower == L"bthle" ||
		   lower == L"bthhfenum" ||
		   lower == L"btha2dp" ||
		   lower == L"bluetooth" ||
		   lower.find(L"bluetooth") != std::wstring::npos;
}

std::vector<GUID> bluetoothDeviceContainerIds() {
	std::vector<GUID> containerIds;
	HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (deviceInfoSet == INVALID_HANDLE_VALUE) {
		return containerIds;
	}

	for (DWORD index = 0;; ++index) {
		SP_DEVINFO_DATA deviceInfoData{};
		deviceInfoData.cbSize = sizeof(deviceInfoData);
		if (!SetupDiEnumDeviceInfo(deviceInfoSet, index, &deviceInfoData)) {
			break;
		}

		const std::wstring enumeratorName = devicePropertyString(
			deviceInfoSet,
			deviceInfoData,
			DEVPKEY_Device_EnumeratorName);
		const std::wstring instanceId = deviceInstanceId(deviceInfoSet, deviceInfoData);
		if (!isBluetoothEnumeratorName(enumeratorName) && !wideTextContainsBluetoothIdHint(instanceId)) {
			continue;
		}

		const std::optional<GUID> containerId = devicePropertyGuid(
			deviceInfoSet,
			deviceInfoData,
			DEVPKEY_Device_ContainerId);
		if (!containerId) {
			continue;
		}

		const bool alreadyPresent = std::any_of(
			containerIds.begin(),
			containerIds.end(),
			[&containerId](const GUID& existing) {
				return IsEqualGUID(existing, *containerId);
			});
		if (!alreadyPresent) {
			containerIds.push_back(*containerId);
		}
	}

	SetupDiDestroyDeviceInfoList(deviceInfoSet);
	return containerIds;
}

class ScopedComInitialiser {
public:
	ScopedComInitialiser()
		: result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
	}

	~ScopedComInitialiser() {
		if (result_ == S_OK || result_ == S_FALSE) {
			CoUninitialize();
		}
	}

	bool available() const {
		return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
	}

private:
	HRESULT result_;
};

std::wstring endpointStringProperty(IPropertyStore* propertyStore, const PROPERTYKEY& key) {
	if (propertyStore == nullptr) {
		return {};
	}

	PROPVARIANT value;
	PropVariantInit(&value);
	std::wstring result;
	if (SUCCEEDED(propertyStore->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
		result = value.pwszVal;
	}
	PropVariantClear(&value);
	return result;
}

std::optional<GUID> endpointContainerId(IPropertyStore* propertyStore) {
	if (propertyStore == nullptr) {
		return std::nullopt;
	}

	PROPVARIANT value;
	PropVariantInit(&value);
	GUID containerId{};
	const bool found =
		SUCCEEDED(propertyStore->GetValue(PKEY_Device_ContainerId, &value)) &&
		propVariantToGuid(value, containerId);
	PropVariantClear(&value);
	if (found) {
		return containerId;
	}
	return std::nullopt;
}

bool endpointContainerIsBluetooth(const GUID& endpointContainerId) {
	const std::vector<GUID> bluetoothContainers = bluetoothDeviceContainerIds();
	return std::any_of(
		bluetoothContainers.begin(),
		bluetoothContainers.end(),
		[&endpointContainerId](const GUID& bluetoothContainerId) {
			return IsEqualGUID(endpointContainerId, bluetoothContainerId);
		});
}

bool endpointPropertiesSuggestBluetooth(IMMDevice* device) {
	IPropertyStore* propertyStore = nullptr;
	if (FAILED(device->OpenPropertyStore(STGM_READ, &propertyStore)) || propertyStore == nullptr) {
		return false;
	}

	bool bluetooth = false;
	if (const std::optional<GUID> containerId = endpointContainerId(propertyStore)) {
		bluetooth = endpointContainerIsBluetooth(*containerId);
	}

	if (!bluetooth) {
		bluetooth =
			wideTextContainsBluetoothHint(endpointStringProperty(propertyStore, PKEY_Device_FriendlyName)) ||
			wideTextContainsBluetoothHint(endpointStringProperty(propertyStore, PKEY_Device_DeviceDesc)) ||
			wideTextContainsBluetoothHint(endpointStringProperty(propertyStore, PKEY_DeviceInterface_FriendlyName));
	}

	propertyStore->Release();
	return bluetooth;
}

bool windowsWasapiInputIsBluetooth(const PaDeviceIndex paIndex, const PaDeviceInfo& paDeviceInfo) {
	const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(paDeviceInfo.hostApi);
	if (hostApiInfo == nullptr || hostApiInfo->type != paWASAPI) {
		return textContainsBluetoothHint(paDeviceInfo.name != nullptr ? paDeviceInfo.name : "");
	}

	ScopedComInitialiser com;
	if (!com.available()) {
		return textContainsBluetoothHint(paDeviceInfo.name != nullptr ? paDeviceInfo.name : "");
	}

	void* rawDevice = nullptr;
	if (PaWasapi_GetIMMDevice(paIndex, &rawDevice) != paNoError || rawDevice == nullptr) {
		return textContainsBluetoothHint(paDeviceInfo.name != nullptr ? paDeviceInfo.name : "");
	}

	auto* device = static_cast<IMMDevice*>(rawDevice);
	bool bluetooth = endpointPropertiesSuggestBluetooth(device);
	LPWSTR endpointId = nullptr;
	if (!bluetooth && SUCCEEDED(device->GetId(&endpointId)) && endpointId != nullptr) {
		bluetooth = wideTextContainsBluetoothIdHint(endpointId);
	}
	if (endpointId != nullptr) {
		CoTaskMemFree(endpointId);
	}

	return bluetooth;
}

bool shouldProtectPassiveLevelMonitoring(const PaDeviceIndex paIndex, const PaDeviceInfo& paDeviceInfo) {
	return windowsWasapiInputIsBluetooth(paIndex, paDeviceInfo);
}

bool shouldUseMonoCoreAudioInput(const PaDeviceInfo&) {
	return false;
}
#endif

}

AudioInput::AudioInput()
	: stream(nullptr),
	  dcFilter(0.995f),
	  noiseGate(0.0001f),
	  sampleRate(44100.0f),
	  channelCount(1),
	  activeChannel(0),
	  leftLevel(0.0f),
	  rightLevel(0.0f) {
#ifdef __linux__
	ALSAErrorSuppressor suppressor;
#endif
	if (const PaError err = Pa_Initialize(); err != paNoError) {
		throw std::runtime_error("PortAudio initialisation failed: " +
								 std::string(Pa_GetErrorText(err)));
	}

	processor.start();
}

AudioInput::~AudioInput() {
	stopStream();
	Pa_Terminate();

	processor.stop();
}

std::vector<AudioInput::DeviceInfo> AudioInput::getInputDevices() {
	std::vector<DeviceInfo> devices;

#ifdef __linux__
	ALSAErrorSuppressor suppressor;
#endif

	const int deviceCount = Pa_GetDeviceCount();

	if (deviceCount < 0) {
		std::cerr << "AudioInput: Failed to get device count: " << Pa_GetErrorText(deviceCount) << std::endl;
		return devices;
	}

	devices.reserve(static_cast<size_t>(deviceCount));
	for (int i = 0; i < deviceCount; ++i) {
		if (const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i)) {
			if (deviceInfo->maxInputChannels > 0) {
				const bool passiveMonitoringProtected =
					shouldProtectPassiveLevelMonitoring(i, *deviceInfo);
				devices.emplace_back(DeviceInfo{
					deviceInfo->name,
					i,
					deviceInfo->maxInputChannels,
					!passiveMonitoringProtected,
					passiveMonitoringProtected
				});
			}
		}
	}
	return devices;
}

bool AudioInput::initStream(const int deviceIndex, const int numChannels) {
	stopStream();

	const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
	if (!deviceInfo) {
		std::cerr << "AudioInput: Failed to get device info for index " << deviceIndex << std::endl;
		return false;
	}
	if (deviceInfo->maxInputChannels < 1) {
		std::cerr << "AudioInput: Device has no input channels" << std::endl;
		return false;
	}

	channelCount = std::min(numChannels, deviceInfo->maxInputChannels);
	if (channelCount < 1)
		channelCount = 1;
	if (shouldUseMonoCoreAudioInput(*deviceInfo)) {
		channelCount = 1;
	}

	activeChannel = 0;
	dcFilter.setChannelCount(static_cast<size_t>(channelCount));

	PaStreamParameters inputParameters{};
	inputParameters.device = deviceIndex;
	inputParameters.channelCount = channelCount;
	inputParameters.sampleFormat = paFloat32;
	inputParameters.suggestedLatency = deviceInfo->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = nullptr;

#ifdef __APPLE__
	PaMacCoreStreamInfo macStreamInfo{};
	std::array<SInt32, 1> inputChannelMap{};
	if (isCoreAudioInputDevice(*deviceInfo) && channelCount == 1) {
		inputChannelMap[0] = preferredCoreAudioInputChannel(deviceIndex, *deviceInfo).value_or(0);
		PaMacCore_SetupStreamInfo(&macStreamInfo, paMacCorePlayNice);
		PaMacCore_SetupChannelMap(&macStreamInfo, inputChannelMap.data(), inputChannelMap.size());
		inputParameters.hostApiSpecificStreamInfo = &macStreamInfo;
	}
#endif

	const PaError err =
		Pa_OpenStream(&stream, &inputParameters, nullptr, deviceInfo->defaultSampleRate,
					  paFramesPerBufferUnspecified, paClipOff, audioCallback, this);

	if (err != paNoError) {
		std::cerr << "AudioInput: Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
		stream = nullptr;
		return false;
	}

	const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream);
	if (!streamInfo) {
		std::cerr << "AudioInput: Failed to get stream info" << std::endl;
		stopStream();
		return false;
	}
	sampleRate = static_cast<float>(streamInfo->sampleRate);

	if (const PaError startErr = Pa_StartStream(stream); startErr != paNoError) {
		std::cerr << "AudioInput: Failed to start stream: " << Pa_GetErrorText(startErr) << std::endl;
		stopStream();
		return false;
	}

	return true;
}

AudioProcessor::SpectralData AudioInput::getSpectralData() const {
	return processor.getSpectralData();
}

bool AudioInput::pauseStream() {
	if (stream && Pa_IsStreamActive(stream) == 1) {
		if (Pa_StopStream(stream) == paNoError) {
			return true;
		}
	}
	return false;
}

bool AudioInput::resumeStream() {
	if (stream && Pa_IsStreamStopped(stream) == 1) {
		if (Pa_StartStream(stream) == paNoError) {
			return true;
		}
	}
	return false;
}

void AudioInput::closeStream() {
	stopStream();
}

bool AudioInput::isStreamActive() const {
	return stream && Pa_IsStreamActive(stream) == 1;
}

void AudioInput::stopStream() {
	if (stream) {
		Pa_StopStream(stream);
		Pa_CloseStream(stream);
		stream = nullptr;
	}
	resetStereoLevels();
}

void AudioInput::updateStereoLevels(const float left, const float right) {
	leftLevel.store(smoothedLevel(leftLevel.load(), std::clamp(left, 0.0f, 1.0f)));
	rightLevel.store(smoothedLevel(rightLevel.load(), std::clamp(right, 0.0f, 1.0f)));
}

void AudioInput::resetStereoLevels() {
	leftLevel.store(0.0f);
	rightLevel.store(0.0f);
}

int AudioInput::audioCallback(const void* input, [[maybe_unused]] void* output,
							  const unsigned long frameCount,
							  [[maybe_unused]] const PaStreamCallbackTimeInfo* timeInfo,
							  [[maybe_unused]] PaStreamCallbackFlags statusFlags,
							  void* userData) {
	auto* audio = static_cast<AudioInput*>(userData);

	if (!input) {
		return paContinue;
	}

	try {
		const auto* inBuffer = static_cast<const float*>(input);
		constexpr size_t MAX_BUFFER_SIZE = 4096;
		thread_local std::vector<float> processedBuffer(MAX_BUFFER_SIZE);
		if (frameCount > MAX_BUFFER_SIZE) {
			processedBuffer.resize(frameCount);
		}

		audio->processor.queueAudioData(inBuffer, frameCount * static_cast<size_t>(audio->channelCount), audio->sampleRate, static_cast<size_t>(audio->channelCount));

		constexpr size_t MAX_LEVEL_CHANNELS = 16;
		std::array<float, MAX_LEVEL_CHANNELS> channelPeaks{};
		const size_t channels = static_cast<size_t>(std::max(audio->channelCount, 1));
		const size_t meteredChannels = std::min(channels, channelPeaks.size());
		for (unsigned long frame = 0; frame < frameCount; ++frame) {
			const size_t offset = static_cast<size_t>(frame) * channels;
			for (size_t ch = 0; ch < meteredChannels; ++ch) {
				channelPeaks[ch] = std::max(channelPeaks[ch], std::abs(inBuffer[offset + ch]));
			}
		}

		float leftPeak = channelPeaks[0];
		float rightPeak = meteredChannels > 1 ? channelPeaks[1] : leftPeak;
		if (meteredChannels > 2) {
			float strongestPeak = 0.0f;
			float secondStrongestPeak = 0.0f;
			for (size_t ch = 0; ch < meteredChannels; ++ch) {
				const float peak = channelPeaks[ch];
				if (peak > strongestPeak) {
					secondStrongestPeak = strongestPeak;
					strongestPeak = peak;
				} else if (peak > secondStrongestPeak) {
					secondStrongestPeak = peak;
				}
			}
			leftPeak = strongestPeak;
			rightPeak = secondStrongestPeak > 0.0f ? secondStrongestPeak : strongestPeak;
		}

		audio->updateStereoLevels(leftPeak, rightPeak);
	}
	catch (const std::exception& e) {
		std::cerr << "[Audio] Exception in audio callback: " << e.what() << std::endl;
		return paContinue;
	}
	catch (...) {
		std::cerr << "[Audio] Unknown exception in audio callback" << std::endl;
		return paContinue;
	}

	return paContinue;
}

struct AudioInputLevelMonitor::MonitoredDevice {
	int paIndex = paNoDevice;
	int channelCount = 0;
	PaStream* stream = nullptr;
	bool startAttempted = false;
	bool allowLevelMonitoring = true;
	std::atomic<float> leftLevel{0.0f};
	std::atomic<float> rightLevel{0.0f};
};

AudioInputLevelMonitor::AudioInputLevelMonitor() = default;

AudioInputLevelMonitor::~AudioInputLevelMonitor() {
	stopAll();
}

void AudioInputLevelMonitor::syncDevices(const std::vector<AudioInput::DeviceInfo>& devices,
										 const int selectedPaIndex) {
	bool topologyChanged = monitoredDevices_.size() != devices.size();
	if (!topologyChanged) {
		for (size_t i = 0; i < devices.size(); ++i) {
			if (monitoredDevices_[i]->paIndex != devices[i].paIndex ||
				monitoredDevices_[i]->allowLevelMonitoring != devices[i].allowLevelMonitoring) {
				topologyChanged = true;
				break;
			}
		}
	}

	if (topologyChanged) {
		stopAll();
		monitoredDevices_.reserve(devices.size());
		for (const auto& device : devices) {
			auto monitoredDevice = std::make_unique<MonitoredDevice>();
			monitoredDevice->paIndex = device.paIndex;
			monitoredDevice->allowLevelMonitoring = device.allowLevelMonitoring;
			monitoredDevices_.push_back(std::move(monitoredDevice));
		}
	}

	for (const auto& device : monitoredDevices_) {
		if (!device) {
			continue;
		}

		if (!device->allowLevelMonitoring) {
			stopDevice(*device);
			device->startAttempted = true;
			continue;
		}

		if (device->paIndex == selectedPaIndex) {
			stopDevice(*device);
			device->startAttempted = false;
			continue;
		}

		if (!device->stream && !device->startAttempted) {
			startDevice(*device);
		}
	}
}

std::array<float, 2> AudioInputLevelMonitor::getStereoLevels(const size_t deviceListIndex) const {
	if (deviceListIndex >= monitoredDevices_.size() || !monitoredDevices_[deviceListIndex]) {
		return {0.0f, 0.0f};
	}

	const MonitoredDevice& device = *monitoredDevices_[deviceListIndex];
	return {device.leftLevel.load(), device.rightLevel.load()};
}

void AudioInputLevelMonitor::stopAll() {
	for (const auto& device : monitoredDevices_) {
		if (device) {
			stopDevice(*device);
		}
	}
	monitoredDevices_.clear();
}

void AudioInputLevelMonitor::stopDevice(MonitoredDevice& device) {
	if (device.stream) {
		Pa_StopStream(device.stream);
		Pa_CloseStream(device.stream);
		device.stream = nullptr;
	}
	device.channelCount = 0;
	device.leftLevel.store(0.0f);
	device.rightLevel.store(0.0f);
}

bool AudioInputLevelMonitor::startDevice(MonitoredDevice& device) {
	device.startAttempted = true;

	const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(device.paIndex);
	if (!deviceInfo || deviceInfo->maxInputChannels < 1) {
		return false;
	}
	if (!device.allowLevelMonitoring) {
		return false;
	}

	device.channelCount = shouldUseMonoCoreAudioInput(*deviceInfo)
		? 1
		: std::min(2, deviceInfo->maxInputChannels);

	PaStreamParameters inputParameters{};
	inputParameters.device = device.paIndex;
	inputParameters.channelCount = device.channelCount;
	inputParameters.sampleFormat = paFloat32;
	inputParameters.suggestedLatency = deviceInfo->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = nullptr;

#ifdef __APPLE__
	PaMacCoreStreamInfo macStreamInfo{};
	std::array<SInt32, 1> inputChannelMap{};
	if (isCoreAudioInputDevice(*deviceInfo) && device.channelCount == 1) {
		inputChannelMap[0] = preferredCoreAudioInputChannel(device.paIndex, *deviceInfo).value_or(0);
		PaMacCore_SetupStreamInfo(&macStreamInfo, paMacCorePlayNice);
		PaMacCore_SetupChannelMap(&macStreamInfo, inputChannelMap.data(), inputChannelMap.size());
		inputParameters.hostApiSpecificStreamInfo = &macStreamInfo;
	}
#endif

	PaStream* stream = nullptr;
	const PaError openErr = Pa_OpenStream(
		&stream,
		&inputParameters,
		nullptr,
		deviceInfo->defaultSampleRate,
		256,
		paClipOff,
		monitorCallback,
		&device);

	if (openErr != paNoError) {
		device.channelCount = 0;
		return false;
	}

	const PaError startErr = Pa_StartStream(stream);
	if (startErr != paNoError) {
		Pa_CloseStream(stream);
		device.channelCount = 0;
		return false;
	}

	device.stream = stream;
	return true;
}

int AudioInputLevelMonitor::monitorCallback(const void* input, [[maybe_unused]] void* output,
											const unsigned long frameCount,
											[[maybe_unused]] const PaStreamCallbackTimeInfo* timeInfo,
											[[maybe_unused]] PaStreamCallbackFlags statusFlags,
											void* userData) {
	auto* device = static_cast<MonitoredDevice*>(userData);
	if (!device || !input || device->channelCount < 1) {
		return paContinue;
	}

	const auto* inBuffer = static_cast<const float*>(input);
	float leftPeak = 0.0f;
	float rightPeak = 0.0f;
	const size_t channels = static_cast<size_t>(device->channelCount);
	for (unsigned long frame = 0; frame < frameCount; ++frame) {
		const size_t offset = static_cast<size_t>(frame) * channels;
		const float left = std::abs(inBuffer[offset]);
		const float right = channels > 1 ? std::abs(inBuffer[offset + 1]) : left;
		leftPeak = std::max(leftPeak, left);
		rightPeak = std::max(rightPeak, right);
	}

	device->leftLevel.store(smoothedLevel(device->leftLevel.load(), std::clamp(leftPeak, 0.0f, 1.0f)));
	device->rightLevel.store(smoothedLevel(device->rightLevel.load(), std::clamp(rightPeak, 0.0f, 1.0f)));
	return paContinue;
}
