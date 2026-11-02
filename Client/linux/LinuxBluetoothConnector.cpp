#include "LinuxBluetoothConnector.h"

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <stdio.h>
#include <unistd.h>

#include <string.h>
#include <ctype.h>
#include <cctype>
#include <stdlib.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <cerrno>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#include "DBusHelper.h"

namespace {

std::string normalizeBluetoothAddress(std::string value)
{
  for (auto &character : value)
  {
    if (character == '-')
    {
      character = ':';
    }
    else
    {
      character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
  }
  return value;
}

} // namespace

LinuxBluetoothConnector::LinuxBluetoothConnector()
{
}
LinuxBluetoothConnector::~LinuxBluetoothConnector()
{
  disconnect();
}

int LinuxBluetoothConnector::recv(char *buf, size_t length)
{
  ssize_t bytesRead = ::read(this->_socket, buf, length);
  if (bytesRead < 0)
  {
    // A recv timeout (SO_RCVTIMEO) is expected while probing optional features: report it as recoverable
    // WITHOUT forcing a disconnect so an unanswered GET just hides that feature instead of dropping the link.
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      throw RecoverableException("recv timed out", false);
    }
    throw RecoverableException(std::string("Couldn't recv: ") + strerror(errno), true);
  }
  return (int)bytesRead;
}

int LinuxBluetoothConnector::send(char *buf, size_t length)
{
  const ssize_t written = ::write(this->_socket, buf, length);
  if (written < 0)
  {
    throw RecoverableException(std::string("Couldn't send: ") + strerror(errno), true);
  }
  return static_cast<int>(written);
}

void LinuxBluetoothConnector::connect(const std::string &addrStr)
{
  // A failed connection attempt must never replace an existing descriptor
  // before the new socket has completed all setup and negotiation.
  disconnect();

  const auto targetAddress = normalizeBluetoothAddress(addrStr);
  bool paired = false;
  for (const auto &device : getConnectedDevices())
  {
    if (device.paired.value_or(false) &&
        normalizeBluetoothAddress(device.mac) == targetAddress)
    {
      paired = true;
      break;
    }
  }
  if (!paired)
  {
    throw RecoverableException(
        "Bluetooth device is not present in the paired BlueZ device list", true);
  }

  struct sockaddr_rc addr = {0};
  const int socketFd = ::socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
  if (socketFd < 0)
  {
    throw RecoverableException(std::string("Couldn't create bluetooth socket: ") +
                                   strerror(errno),
                               true);
  }

  try
  {
    uint32_t linkmode = RFCOMM_LM_AUTH | RFCOMM_LM_ENCRYPT;
    if (::setsockopt(socketFd, SOL_RFCOMM, RFCOMM_LM, &linkmode,
                     sizeof(linkmode)) < 0)
    {
      throw RecoverableException(
          std::string("Couldn't enable RFCOMM authentication/encryption: ") +
              strerror(errno),
          true);
    }

    // Bound recv so capability probing can't block forever (matches the 2.5s macOS timeout).
    struct timeval recvTimeout = {2, 500000};
    if (::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout,
                     sizeof(recvTimeout)) < 0)
    {
      throw RecoverableException(
          std::string("Couldn't set bluetooth receive timeout: ") +
              strerror(errno),
          true);
    }

    // set the connection parameters (who to connect to)
    addr.rc_family = AF_BLUETOOTH;
    // try the v1 service UUID first, then fall back to the v2 (newer-generation) UUID.
    SonyProtocolVersion negotiatedVersion = SonyProtocolVersion::V1;
    uint8_t channel =
        sdp_getServiceChannel(targetAddress.c_str(), SERVICE_UUID_IN_BYTES);
    if (channel == 0)
    {
      channel =
          sdp_getServiceChannel(targetAddress.c_str(), SERVICE_UUID_V2_IN_BYTES);
      negotiatedVersion = SonyProtocolVersion::V2;
    }
    if (channel == 0)
    {
      throw RecoverableException(
          "Couldn't find the Sony service record on this device (neither protocol version) - is this a supported headset?",
          true);
    }
    addr.rc_channel = channel;
    str2ba(targetAddress.c_str(), &addr.rc_bdaddr);

    // connect to server
    if (::connect(socketFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      throw RecoverableException(
          std::string("Error: could not connect to bluetooth socket: ") +
              strerror(errno),
          true);
    }

    this->_socket = socketFd;
    this->_protocolVersion = negotiatedVersion;
    this->_connected = true;
  }
  catch (...)
  {
    ::close(socketFd);
    throw;
  }
}

std::vector<BluetoothDevice> LinuxBluetoothConnector::getConnectedDevices()
{

  std::vector<BluetoothDevice> res;

  DBusConnection *connection = dbus_open_system_bus();
  if (NULL == connection)
  {
    return res;
  }

  std::vector<std::string> adapter_paths = dbus_list_adapters(connection);
  for (auto &adapter : adapter_paths)
  {
    std::string name = dbus_get_property(connection, adapter.c_str(), "Name");
    std::string address = dbus_get_property(connection, adapter.c_str(), "Address");
    bool paired = dbus_get_property_bool(connection, adapter.c_str(), "Paired");
    if (!paired) continue;
    bool connected = dbus_get_property_bool(connection, adapter.c_str(), "Connected");
    if (connected)
    {
      res.insert(res.begin(), {.name = name, .mac = address, .paired = paired, .connected = connected});
    }
    else
    {
      res.push_back({.name = name, .mac = address, .paired = paired, .connected = connected});
    }
  }

  dbus_connection_unref(connection);
  return res;
}

void LinuxBluetoothConnector::disconnect() noexcept
{
  // close connection
  if (this->_socket != -1)
  {
    ::close(this->_socket);
    this->_socket = -1;
  }
  this->_connected = false;
}

void LinuxBluetoothConnector::cancelPendingIo() noexcept
{
  if (this->_socket != -1)
  {
    ::shutdown(this->_socket, SHUT_RDWR);
  }
}

bool LinuxBluetoothConnector::isConnected() noexcept
{
  return this->_connected;
}

SonyProtocolVersion LinuxBluetoothConnector::getProtocolVersion() noexcept
{
  return this->_protocolVersion;
}
