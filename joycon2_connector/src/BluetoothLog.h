#pragma once
// BluetoothLog - Human-readable descriptions for WinRT Bluetooth/GATT errors,
// intended to be used together with the APP_LOG_* macros.

#include "Logger.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace BluetoothLog {

using winrt::Windows::Foundation::IReference;
using winrt::Windows::Devices::Bluetooth::BluetoothAddressType;
using winrt::Windows::Devices::Bluetooth::BluetoothConnectionStatus;
using winrt::Windows::Devices::Bluetooth::BluetoothError;
using winrt::Windows::Devices::Bluetooth::BluetoothLEPreferredConnectionParametersRequestStatus;
using winrt::Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementType;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCommunicationStatus;

inline const char* AdvertisementTypeName(BluetoothLEAdvertisementType type) {
    switch (type) {
    case BluetoothLEAdvertisementType::ConnectableUndirected:    return "ConnectableUndirected";
    case BluetoothLEAdvertisementType::ConnectableDirected:      return "ConnectableDirected";
    case BluetoothLEAdvertisementType::ScannableUndirected:      return "ScannableUndirected";
    case BluetoothLEAdvertisementType::NonConnectableUndirected: return "NonConnectableUndirected";
    case BluetoothLEAdvertisementType::ScanResponse:             return "ScanResponse";
    case BluetoothLEAdvertisementType::Extended:                 return "Extended";
    default:                                                     return "Unknown";
    }
}

inline const char* AdvertisementConnectabilityName(BluetoothLEAdvertisementType type) {
    switch (type) {
    case BluetoothLEAdvertisementType::ConnectableUndirected:
    case BluetoothLEAdvertisementType::ConnectableDirected:
        return "yes";
    case BluetoothLEAdvertisementType::ScannableUndirected:
    case BluetoothLEAdvertisementType::NonConnectableUndirected:
        return "no";
    case BluetoothLEAdvertisementType::ScanResponse:
    default:
        return "unknown";
    }
}

inline const char* AddressTypeName(BluetoothAddressType type) {
    switch (type) {
    case BluetoothAddressType::Public:      return "Public";
    case BluetoothAddressType::Random:      return "Random";
    case BluetoothAddressType::Unspecified: return "Unspecified";
    default:                                return "Unknown";
    }
}

inline std::string BytesToHex(const uint8_t* data, size_t size) {
    if (!data || size == 0) return "<empty>";

    std::string result;
    result.reserve(size * 3 - 1);
    for (size_t i = 0; i < size; ++i) {
        char byte[3];
        sprintf_s(byte, "%02X", data[i]);
        if (i != 0) result += ' ';
        result += byte;
    }
    return result;
}

inline const char* BluetoothErrorName(BluetoothError error) {
    switch (error) {
    case BluetoothError::Success:               return "Success";
    case BluetoothError::RadioNotAvailable:     return "RadioNotAvailable (Bluetooth radio is off or unavailable)";
    case BluetoothError::ResourceInUse:         return "ResourceInUse (Bluetooth resource is being used by another app/service)";
    case BluetoothError::DeviceNotConnected:    return "DeviceNotConnected";
    case BluetoothError::OtherError:            return "OtherError";
    case BluetoothError::DisabledByPolicy:      return "DisabledByPolicy";
    case BluetoothError::NotSupported:          return "NotSupported";
    case BluetoothError::DisabledByUser:        return "DisabledByUser (Bluetooth is turned off)";
    case BluetoothError::ConsentRequired:       return "ConsentRequired";
    case BluetoothError::TransportNotSupported: return "TransportNotSupported";
    default:                                    return "Unknown";
    }
}

inline std::string DescribeBluetoothError(BluetoothError error) {
    char buffer[96];
    sprintf_s(buffer, "%s (%d)", BluetoothErrorName(error), static_cast<int>(error));
    return buffer;
}

inline const char* GattCommunicationStatusName(GattCommunicationStatus status) {
    switch (status) {
    case GattCommunicationStatus::Success:       return "Success";
    case GattCommunicationStatus::Unreachable:   return "Unreachable (device is not connected or is out of range)";
    case GattCommunicationStatus::ProtocolError: return "ProtocolError";
    case GattCommunicationStatus::AccessDenied:  return "AccessDenied";
    default:                                     return "Unknown";
    }
}

inline const char* GattProtocolErrorName(uint8_t code) {
    switch (code) {
    case 0x01: return "Invalid Handle";
    case 0x02: return "Read Not Permitted";
    case 0x03: return "Write Not Permitted";
    case 0x04: return "Invalid PDU";
    case 0x05: return "Insufficient Authentication";
    case 0x06: return "Request Not Supported";
    case 0x07: return "Invalid Offset";
    case 0x08: return "Insufficient Authorization";
    case 0x09: return "Prepare Queue Full";
    case 0x0A: return "Attribute Not Found";
    case 0x0B: return "Attribute Not Long";
    case 0x0C: return "Insufficient Encryption Key Size";
    case 0x0D: return "Invalid Attribute Value Length";
    case 0x0E: return "Unlikely Error";
    case 0x0F: return "Insufficient Encryption";
    case 0x10: return "Unsupported Group Type";
    case 0x11: return "Insufficient Resources";
    default:
        if (code >= 0x80 && code <= 0x9F) return "Application Error";
        return nullptr;
    }
}

inline std::string DescribeGattStatus(GattCommunicationStatus status, const IReference<uint8_t>& protocolError = nullptr) {
    std::string result = GattCommunicationStatusName(status);
    result += " (";
    result += std::to_string(static_cast<int>(status));
    result += ")";

    if (protocolError) {
        uint8_t code = protocolError.Value();
        char codeText[48];
        sprintf_s(codeText, ", protocol_error=0x%02X", code);

        const char* name = GattProtocolErrorName(code);
        if (name) {
            result += codeText;
            result += " (";
            result += name;
            result += ")";
        } else {
            result += codeText;
        }
    }
    return result;
}

inline const char* ConnectionStatusName(BluetoothConnectionStatus status) {
    switch (status) {
    case BluetoothConnectionStatus::Disconnected: return "Disconnected";
    case BluetoothConnectionStatus::Connected:    return "Connected";
    default:                                      return "Unknown";
    }
}

inline const char* ConnectionParametersRequestStatusName(BluetoothLEPreferredConnectionParametersRequestStatus status) {
    switch (status) {
    case BluetoothLEPreferredConnectionParametersRequestStatus::Unspecified:      return "Unspecified";
    case BluetoothLEPreferredConnectionParametersRequestStatus::Success:          return "Success";
    case BluetoothLEPreferredConnectionParametersRequestStatus::DeviceNotAvailable: return "DeviceNotAvailable";
    case BluetoothLEPreferredConnectionParametersRequestStatus::AccessDenied:     return "AccessDenied";
    default:                                                                      return "Unknown";
    }
}

inline std::string DescribeConnectionParametersRequestStatus(BluetoothLEPreferredConnectionParametersRequestStatus status) {
    char buffer[96];
    sprintf_s(buffer, "%s (%d)", ConnectionParametersRequestStatusName(status), static_cast<int>(status));
    return buffer;
}

inline std::string GuidToString(winrt::guid const& uuid) {
    return winrt::to_string(winrt::to_hstring(uuid));
}

// Formats a C++/WinRT exception with its HRESULT and, when available, its message.
inline std::string DescribeHResultError(const winrt::hresult_error& error) {
    char code[32];
    sprintf_s(code, "HRESULT 0x%08X", static_cast<unsigned int>(error.code().value));

    std::string result = code;
    winrt::hstring message = error.message();
    if (!message.empty()) {
        result += " - ";
        result += winrt::to_string(message);
    }
    return result;
}

} // namespace BluetoothLog
