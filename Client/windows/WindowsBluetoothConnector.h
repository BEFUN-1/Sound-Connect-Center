#pragma once
#include <stdio.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "Bthprops.lib")
#include <winsock2.h>
#include <ws2bth.h>
#include <BluetoothAPIs.h>
#include <iostream>
#include <rpc.h>
#include "IBluetoothConnector.h"
#include <string>
#include "ByteMagic.h"
#include <atomic>
#include <chrono>
#include <mutex>

class WindowsBluetoothConnector final : public IBluetoothConnector
{
public:
	WindowsBluetoothConnector();

	~WindowsBluetoothConnector();

	virtual void connect(const std::string& addrStr) noexcept(false);
	virtual int send(char* buf, size_t length) noexcept(false);
	virtual int recv(char* buf, size_t length) noexcept(false);
	virtual void cancelPendingIo() noexcept;
	virtual void disconnect() noexcept;
	virtual bool isConnected() noexcept;

	virtual std::vector<BluetoothDevice> getConnectedDevices() noexcept(false);
	std::vector<std::string> getLocalAddresses() override;
	virtual SonyProtocolVersion getProtocolVersion() noexcept;

private:
	std::vector<BluetoothDevice> _findDevicesInRadio(BLUETOOTH_DEVICE_SEARCH_PARAMS* searchParams);
	std::string _wstringToUtf8(const std::wstring& wstr);
	bool _tryConnect(const char* uuid, SOCKADDR_BTH& sab);

	std::atomic<SOCKET> _socket{INVALID_SOCKET};
	std::atomic<bool> _connected = false;
	std::atomic<bool> _cancelled{false};
	std::mutex _socketMutex;
	void _wait(bool readable, std::chrono::steady_clock::time_point deadline);
	SonyProtocolVersion _protocolVersion = SonyProtocolVersion::V1;
	void _initSocket();
};
