#include "WindowsBluetoothConnector.h"
#include <algorithm>
#include <climits>

namespace {
template<class Handle, auto Close>
struct ScopedBluetoothHandle {
	Handle value;
	~ScopedBluetoothHandle() { if (value) Close(value); }
};
}

void WSAStartupWrapper()
{
	int iResult;
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		throw std::runtime_error("WSAStartup failed: " + std::to_string(iResult));
	}
}

WindowsBluetoothConnector::WindowsBluetoothConnector()
{
	static const bool startedUp = [] {
		::WSAStartupWrapper();
		return true;
	}();
	(void)startedUp;
}

bool WindowsBluetoothConnector::_tryConnect(const char* uuid, SOCKADDR_BTH& sab)
{
	RPC_STATUS errCode = ::UuidFromStringA((RPC_CSTR)uuid, &sab.serviceClassId);
	if (errCode != RPC_S_OK)
	{
		throw std::runtime_error("Couldn't create GUID: " + std::to_string(errCode));
	}
	if (_cancelled) throw RecoverableException("Connection cancelled", true);
	if (::connect(_socket.load(), (sockaddr*)&sab, sizeof(sab)) == 0) return true;
	if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
	try {
		// Let Windows determine its normal connection timeout; cancellation is
		// checked every 100 ms instead of blocking inside connect().
		_wait(false, std::chrono::steady_clock::time_point::max());
		int error = 0;
		int size = sizeof(error);
		if (::getsockopt(_socket.load(), SOL_SOCKET, SO_ERROR,
			reinterpret_cast<char*>(&error), &size) != 0) return false;
		WSASetLastError(error);
		return error == 0;
	} catch (...) {
		if (_cancelled) throw;
		return false;
	}
}

void WindowsBluetoothConnector::connect(const std::string& addrStr)
{
	_cancelled = false;
	if (this->_socket == INVALID_SOCKET)
	{
		this->_initSocket();
	}

	SOCKADDR_BTH sab = { 0 };
	sab.addressFamily = AF_BTH;
	sab.btAddr = MACStringToLong(addrStr);

	// try the v1 service UUID first, then fall back to the v2 (newer-generation) UUID.
	if (_tryConnect(SONY_UUID, sab))
	{
		this->_protocolVersion = SonyProtocolVersion::V1;
		this->_connected = true;
		return;
	}

	// A failed connect() can leave the socket unusable - recreate it before trying the v2 UUID.
	{
		std::lock_guard lock(_socketMutex);
		::closesocket(_socket.exchange(INVALID_SOCKET));
	}
	if (_cancelled) throw RecoverableException("Connection cancelled", true);
	this->_initSocket();

	if (_tryConnect(SONY_UUID_V2, sab))
	{
		this->_protocolVersion = SonyProtocolVersion::V2;
		this->_connected = true;
		return;
	}

	throw RecoverableException("Couldn't connect: " + std::to_string(WSAGetLastError()), true);
}

WindowsBluetoothConnector::~WindowsBluetoothConnector()
{
	disconnect();
}

void WindowsBluetoothConnector::_wait(bool readable, std::chrono::steady_clock::time_point deadline)
{
	while (!_cancelled) {
		if (std::chrono::steady_clock::now() >= deadline)
			throw RecoverableException("Socket wait timed out", false);
		fd_set ready, errors;
		FD_ZERO(&ready);
		FD_ZERO(&errors);
		const auto socket = _socket.load();
		if (socket == INVALID_SOCKET) throw RecoverableException("Socket closed", true);
		FD_SET(socket, &ready);
		FD_SET(socket, &errors);
		timeval timeout{0, 100000};
		const auto result = ::select(0, readable ? &ready : nullptr,
			readable ? nullptr : &ready, &errors, &timeout);
		if (result == SOCKET_ERROR) throw RecoverableException("Socket wait failed", true);
		if (result > 0) return;
	}
	throw RecoverableException("Bluetooth operation cancelled", true);
}

int WindowsBluetoothConnector::send(char* buf, size_t length)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	int bytesSent;
	do {
		if (_cancelled) throw RecoverableException("Send cancelled", true);
		bytesSent = ::send(_socket.load(), buf, static_cast<int>((std::min)(length, size_t{INT_MAX})), 0);
		if (bytesSent != SOCKET_ERROR || WSAGetLastError() != WSAEWOULDBLOCK) break;
		_wait(false, deadline);
	} while (true);
	if (bytesSent == SOCKET_ERROR)
	{
		throw RecoverableException("Couldn't send (" + std::to_string(WSAGetLastError()) + ")", true);
	}
	return bytesSent;
}

int WindowsBluetoothConnector::recv(char* buf, size_t length)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
	int bytesReceived;
	do {
		if (_cancelled) throw RecoverableException("Receive cancelled", true);
		bytesReceived = ::recv(_socket.load(), buf, static_cast<int>((std::min)(length, size_t{INT_MAX})), 0);
		if (bytesReceived != SOCKET_ERROR || WSAGetLastError() != WSAEWOULDBLOCK) break;
		_wait(true, deadline);
	} while (true);
	if (bytesReceived == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		// A recv timeout is expected during capability probing - surface it as recoverable WITHOUT forcing a
		// disconnect, so an unanswered optional-feature GET just hides that feature instead of dropping the link.
		if (err == WSAETIMEDOUT)
		{
			throw RecoverableException("recv timed out", false);
		}
		throw RecoverableException("Couldn't recv (" + std::to_string(err) + ")", true);
	}
	return bytesReceived;
}

std::vector<BluetoothDevice> WindowsBluetoothConnector::getConnectedDevices()
{
	std::vector<BluetoothDevice> res;
	std::vector<BluetoothDevice> devsInRadio;

	HANDLE radio = NULL;
	BLUETOOTH_FIND_RADIO_PARAMS radioSearchParams = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
	HBLUETOOTH_RADIO_FIND radioFindHandle = NULL;

	// Include paired devices so the UI can offer an explicit connection attempt.
	BLUETOOTH_DEVICE_SEARCH_PARAMS dev_search_params{};
	dev_search_params.dwSize = sizeof(dev_search_params);
	dev_search_params.fReturnAuthenticated = TRUE;
	dev_search_params.fReturnRemembered = TRUE;
	dev_search_params.fReturnConnected = TRUE;

	// Iterate for available bluetooth radio devices
	radioFindHandle = BluetoothFindFirstRadio(&radioSearchParams, &radio);
	if (!radioFindHandle)
	{
		if (ERROR_NO_MORE_ITEMS == GetLastError())
		{
			throw RecoverableException(NO_BLUETOOTH_DEVICES_ERROR);
		}
		else
		{
			throw std::runtime_error("BluetoothFindFirstRadio() failed with error code " + std::to_string(GetLastError()));
		}
	}
	ScopedBluetoothHandle<HBLUETOOTH_RADIO_FIND, BluetoothFindRadioClose> search{radioFindHandle};

	do {
		ScopedBluetoothHandle<HANDLE, CloseHandle> radioOwner{radio};
		dev_search_params.hRadio = radio;
		devsInRadio = _findDevicesInRadio(&dev_search_params);
		res.insert(res.end(), devsInRadio.begin(), devsInRadio.end());
	} while (BluetoothFindNextRadio(radioFindHandle, &radio));

	return res;
}

std::vector<std::string> WindowsBluetoothConnector::getLocalAddresses()
{
	std::vector<std::string> addresses;
	BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)};
	HANDLE radio = nullptr;
	const auto find = BluetoothFindFirstRadio(&params, &radio);
	if (!find) return addresses;
	ScopedBluetoothHandle<HBLUETOOTH_RADIO_FIND, BluetoothFindRadioClose> search{find};
	do {
		ScopedBluetoothHandle<HANDLE, CloseHandle> owner{radio};
		BLUETOOTH_RADIO_INFO info{sizeof(info)};
		if (BluetoothGetRadioInfo(radio, &info) == ERROR_SUCCESS)
			addresses.push_back(MACBytesToString(info.address.rgBytes));
	} while (BluetoothFindNextRadio(find, &radio));
	return addresses;
}

void WindowsBluetoothConnector::disconnect() noexcept
{
	std::lock_guard lock(_socketMutex);
	const auto socket = _socket.exchange(INVALID_SOCKET);
	this->_connected = false;
	if (socket != INVALID_SOCKET)
	{
		shutdown(socket, SD_BOTH);
		closesocket(socket);
	}
}

void WindowsBluetoothConnector::cancelPendingIo() noexcept
{
	_cancelled = true;
	std::lock_guard lock(_socketMutex);
	if (this->_socket != INVALID_SOCKET)
	{
		shutdown(_socket.load(), SD_BOTH);
	}
}

bool WindowsBluetoothConnector::isConnected() noexcept
{
	return this->_connected;
}

SonyProtocolVersion WindowsBluetoothConnector::getProtocolVersion() noexcept
{
	return this->_protocolVersion;
}

void WindowsBluetoothConnector::_initSocket()
{
	SOCKET sock = ::socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
	if (sock == INVALID_SOCKET)
	{
		throw std::runtime_error("Couldn't create socket: " + std::to_string(WSAGetLastError()));
	}

	try
	{
		ULONG enable = TRUE;
		if (::setsockopt(sock, SOL_RFCOMM, SO_BTH_AUTHENTICATE,
			reinterpret_cast<char*>(&enable), sizeof(enable)))
		{
			throw std::runtime_error("Couldn't set SO_BTH_AUTHENTICATE: " + std::to_string(WSAGetLastError()));
		}

		if (::setsockopt(sock, SOL_RFCOMM, SO_BTH_ENCRYPT,
			reinterpret_cast<char*>(&enable), sizeof(enable)))
		{
			throw std::runtime_error("Couldn't set SO_BTH_ENCRYPT: " + std::to_string(WSAGetLastError()));
		}

		// Bound recv so it can't block forever. Capability probing sends a GET and relies on this timeout to
		// decide a feature is unsupported - without it the app hangs on "Reading device settings" (matches the
		// 2.5s timeout the macOS connector uses).
		DWORD recvTimeoutMs = 2500;
		if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
			reinterpret_cast<char*>(&recvTimeoutMs), sizeof(recvTimeoutMs)))
		{
			throw std::runtime_error("Couldn't set SO_RCVTIMEO: " + std::to_string(WSAGetLastError()));
		}

		u_long nonblocking = 1;
		if (::ioctlsocket(sock, FIONBIO, &nonblocking) != 0)
			throw std::runtime_error("Couldn't enable nonblocking Bluetooth I/O");
		std::lock_guard lock(_socketMutex);
		if (_cancelled) throw RecoverableException("Connection cancelled", true);
		this->_socket = sock;
	}
	catch (...)
	{
		::closesocket(sock);
		throw;
	}
}

std::string WindowsBluetoothConnector::_wstringToUtf8(const std::wstring& wstr)
{
	std::string strTo;
	const int num_chars = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), NULL, 0, NULL, NULL);

	if (num_chars > 0)
	{
		strTo.resize(num_chars);
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), &strTo[0], num_chars, NULL, NULL);
	}
	return strTo;
}

std::vector<BluetoothDevice> WindowsBluetoothConnector::_findDevicesInRadio(BLUETOOTH_DEVICE_SEARCH_PARAMS* search_params)
{
	std::vector<BluetoothDevice> res;

	BLUETOOTH_DEVICE_INFO device_info = { sizeof(BLUETOOTH_DEVICE_INFO),0, };
	HBLUETOOTH_DEVICE_FIND dev_find_handle = NULL;

	// For each radio, get the first device
	dev_find_handle = BluetoothFindFirstDevice(search_params, &device_info);

	if (!dev_find_handle)
	{
		if (ERROR_NO_MORE_ITEMS == GetLastError())
		{
			//No devices were found, so we can just return an empty vector
			return res;
		}
		else
		{
			throw std::runtime_error("BluetoothFindFirstDevice() failed with error code: " + std::to_string(GetLastError()));
		}
	}
	ScopedBluetoothHandle<HBLUETOOTH_DEVICE_FIND, BluetoothFindDeviceClose> search{dev_find_handle};

	// Get the device info
	do {
		res.emplace_back(BluetoothDevice{
			_wstringToUtf8(device_info.szName),
			MACBytesToString(device_info.Address.rgBytes),
			device_info.fAuthenticated || device_info.fRemembered,
			device_info.fConnected
		});
	} while (BluetoothFindNextDevice(dev_find_handle, &device_info));

	return res;
}
