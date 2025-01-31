/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ClientProxyMembershipID.hpp"

#include <ctime>
#include <iostream>
#include <memory>
#include <string>

#include <boost/process/environment.hpp>

#include <geode/CacheableBuiltins.hpp>

#include "DataOutputInternal.hpp"
#include "DistributedSystem.hpp"
#include "Version.hpp"
#include "util/Log.hpp"

#define DCPORT 12334
#define VMKIND 13
#define ROLEARRLENGTH 0

namespace apache {
namespace geode {
namespace client {

static int synch_counter = 2;

const int ClientProxyMembershipID::VERSION_MASK = 0x8;
const int8_t ClientProxyMembershipID::TOKEN_ORDINAL = -1;

ClientProxyMembershipID::ClientProxyMembershipID()
    : hostPort_(0), vmViewId_(0) {}

ClientProxyMembershipID::~ClientProxyMembershipID() noexcept = default;

ClientProxyMembershipID::ClientProxyMembershipID(
    std::string dsName, std::string randString, const std::string& hostname,
    const boost::asio::ip::address& address, uint32_t hostPort,
    const std::string& durableClientId,
    const std::chrono::seconds durableClientTimeOut) {
  auto vmPID = boost::this_process::get_id();

  initHostAddressVector(address);

  initObjectVars(hostname, hostPort, durableClientId, durableClientTimeOut,
                 DCPORT, vmPID, VMKIND, 0, dsName.c_str(), randString.c_str(),
                 0);
}

// This is only for unit tests and should not be used for any other purpose. See
// ClientProxyMembershipIDTest.cpp for more details
ClientProxyMembershipID::ClientProxyMembershipID(
    const uint8_t* hostAddr, uint32_t hostAddrLen, uint32_t hostPort,
    const char* dsname, const char* uniqueTag, uint32_t vmViewId) {
  auto vmPID = boost::this_process::get_id();

  initHostAddressVector(hostAddr, hostAddrLen);

  initObjectVars("localhost", hostPort, "", std::chrono::seconds::zero(),
                 DCPORT, vmPID, VMKIND, 0, dsname, uniqueTag, vmViewId);
}

void ClientProxyMembershipID::initHostAddressVector(
    const boost::asio::ip::address& address) {
  if (address.is_v6()) {
    auto bytes = address.to_v6().to_bytes();
    hostAddr_.assign(bytes.begin(), bytes.end());
  } else {
    auto bytes = address.to_v4().to_bytes();
    hostAddr_.assign(bytes.begin(), bytes.end());
  }
}

void ClientProxyMembershipID::initHostAddressVector(const uint8_t* hostAddr,
                                                    uint32_t hostAddrLen) {
  hostAddr_.assign(hostAddr, hostAddr + hostAddrLen);
}

void ClientProxyMembershipID::initObjectVars(
    const std::string& hostname, uint32_t hostPort,
    const std::string& durableClientId,
    const std::chrono::seconds durableClntTimeOut, int32_t dcPort, int32_t vPID,
    int8_t vmkind, int8_t splitBrainFlag, const char* dsname,
    const char* uniqueTag, uint32_t vmViewId) {
  DataOutputInternal m_memID;
  if (dsname == nullptr) {
    dsName_ = std::string("");
  } else {
    dsName_ = std::string(dsname);
  }
  hostPort_ = hostPort;
  if (uniqueTag == nullptr) {
    uniqueTag_ = std::string("");
  } else {
    uniqueTag_ = std::string(uniqueTag);
  }

  vmViewId_ = vmViewId;
  m_memID.write(static_cast<int8_t>(DSCode::FixedIDByte));
  m_memID.write(static_cast<int8_t>(DSFid::InternalDistributedMember));
  m_memID.writeBytes(hostAddr_.data(), static_cast<int32_t>(hostAddr_.size()));
  m_memID.writeInt(static_cast<int32_t>(synch_counter));
  m_memID.writeString(hostname);
  m_memID.write(splitBrainFlag);

  m_memID.writeInt(dcPort);

  m_memID.writeInt(vPID);
  m_memID.write(vmkind);
  m_memID.writeArrayLen(ROLEARRLENGTH);
  m_memID.writeString(dsname);
  m_memID.writeString(uniqueTag);

  if (!(durableClientId.empty() ||
        durableClntTimeOut == std::chrono::seconds::zero())) {
    m_memID.writeString(durableClientId);
    const auto int32ptr = CacheableInt32::create(
        static_cast<int32_t>(durableClntTimeOut.count()));
    int32ptr->toData(m_memID);
  }
  Version::write(m_memID, Version::current());
  size_t len;
  char* buf =
      reinterpret_cast<char*>(const_cast<uint8_t*>(m_memID.getBuffer(&len)));
  memIdStr_.append(buf, len);

  clientId_.append(hostname);
  clientId_.append("(");
  clientId_.append(std::to_string(vPID));
  clientId_.append(":loner):");
  clientId_.append(std::to_string(synch_counter));
  clientId_.append(":");
  clientId_.append(getUniqueTag());
  clientId_.append(":");
  clientId_.append(getDSName());

  for (uint32_t i = 0; i < getHostAddrLen(); i++) {
    hashKey_.append(":");
    hashKey_.append(std::to_string(hostAddr_[i]));
  }
  hashKey_.append(":");
  hashKey_.append(std::to_string(getHostPort()));
  hashKey_.append(":");
  hashKey_.append(getDSName());
  hashKey_.append(":");
  if (!uniqueTag_.empty()) {
    hashKey_.append(getUniqueTag());
  } else {
    hashKey_.append(":");
    hashKey_.append(std::to_string(vmViewId_));
  }
  LOGDEBUG("GethashKey %s client id: %s ", hashKey_.c_str(), clientId_.c_str());
}

const std::string& ClientProxyMembershipID::getDSMemberId() const {
  return memIdStr_;
}

const std::string& ClientProxyMembershipID::getDSMemberIdForThinClientUse() {
  return clientId_;
}

std::string ClientProxyMembershipID::getHashKey() { return hashKey_; }

void ClientProxyMembershipID::toData(DataOutput&) const {
  throw IllegalStateException("Member ID toData() not implemented.");
}

void ClientProxyMembershipID::fromData(DataInput& input) {
  // deserialization for PR FX HA

  auto length = input.readArrayLength();
  auto hostAddress = new uint8_t[length];
  input.readBytesOnly(hostAddress, length);
  auto hostPort = input.readInt32();
  auto hostname =
      std::dynamic_pointer_cast<CacheableString>(input.readObject());
  auto splitbrain = input.read();
  auto dcport = input.readInt32();
  auto vPID = input.readInt32();
  auto vmKind = input.read();
  auto aStringArray = CacheableStringArray::create();
  aStringArray->fromData(input);
  auto dsName = std::dynamic_pointer_cast<CacheableString>(input.readObject());
  auto uniqueTag =
      std::dynamic_pointer_cast<CacheableString>(input.readObject());
  auto durableClientId =
      std::dynamic_pointer_cast<CacheableString>(input.readObject());
  auto durableClientTimeOut = std::chrono::seconds(input.readInt32());
  int32_t vmViewId = 0;
  readVersion(splitbrain, input);

  initHostAddressVector(hostAddress, length);

  if (vmKind != ClientProxyMembershipID::LONER_DM_TYPE) {
    vmViewId = std::stoi(uniqueTag->value());
    initObjectVars(hostname->value().c_str(), hostPort,
                   durableClientId->value().c_str(), durableClientTimeOut,
                   dcport, vPID, vmKind, splitbrain, dsName->value().c_str(),
                   nullptr, vmViewId);
  } else {
    // initialize the object
    initObjectVars(hostname->value().c_str(), hostPort,
                   durableClientId->value().c_str(), durableClientTimeOut,
                   dcport, vPID, vmKind, splitbrain, dsName->value().c_str(),
                   uniqueTag->value().c_str(), 0);
  }
  delete[] hostAddress;
  readAdditionalData(input);
}

Serializable* ClientProxyMembershipID::readEssentialData(DataInput& input) {
  auto length = input.readArrayLength();
  auto hostAddress = new uint8_t[length];
  input.readBytesOnly(hostAddress, length);
  auto hostPort = input.readInt32();

  // read and ignore flag
  input.read();

  const auto vmKind = input.read();
  int32_t vmViewId = 0;
  std::shared_ptr<CacheableString> uniqueTag, vmViewIdstr;
  if (vmKind == ClientProxyMembershipID::LONER_DM_TYPE) {
    uniqueTag = std::dynamic_pointer_cast<CacheableString>(input.readObject());
  } else {
    vmViewIdstr =
        std::dynamic_pointer_cast<CacheableString>(input.readObject());
    vmViewId = std::stoi(vmViewIdstr->value());
  }

  auto dsName = std::dynamic_pointer_cast<CacheableString>(input.readObject());

  initHostAddressVector(hostAddress, length);

  if (vmKind != ClientProxyMembershipID::LONER_DM_TYPE) {
    // initialize the object with the values read and some dummy values
    initObjectVars("", hostPort, "", std::chrono::seconds::zero(), DCPORT, 0,
                   vmKind, 0, dsName->value().c_str(), nullptr, vmViewId);
  } else {
    // initialize the object with the values read and some dummy values
    initObjectVars("", hostPort, "", std::chrono::seconds::zero(), DCPORT, 0,
                   vmKind, 0, dsName->value().c_str(),
                   uniqueTag->value().c_str(), vmViewId);
  }

  delete[] hostAddress;
  readAdditionalData(input);

  return this;
}

void ClientProxyMembershipID::readAdditionalData(DataInput& input) {
  // Skip unused UUID (16) and weight (0);
  input.advanceCursor(17);
}

void ClientProxyMembershipID::increaseSynchCounter() { ++synch_counter; }

// Compares two membershipIds. This is based on the compareTo function
// of InternalDistributedMember class of Java.
// Any change to the java function should be reflected here as well.
int16_t ClientProxyMembershipID::compareTo(
    const DSMemberForVersionStamp& other) const {
  if (this == &other) {
    return 0;
  }

  const auto& otherMember = dynamic_cast<const ClientProxyMembershipID&>(other);
  auto myPort = getHostPort();
  auto otherPort = otherMember.getHostPort();

  if (myPort < otherPort) return -1;
  if (myPort > otherPort) return 1;

  auto myAddr = getHostAddr();
  auto otherAddr = otherMember.getHostAddr();

  if (myAddr.empty() && otherAddr.empty()) {
    if (myPort < otherPort) {
      return -1;
    } else if (myPort > otherPort) {
      return 1;
    } else {
      return 0;
    }
  } else if (myAddr.empty()) {
    return -1;
  } else if (otherAddr.empty()) {
    return 1;
  }
  for (uint32_t i = 0; i < getHostAddrLen(); i++) {
    if (myAddr[i] < otherAddr[i]) return -1;
    if (myAddr[i] > otherAddr[i]) return 1;
  }

  std::string myUniqueTag = getUniqueTag();
  std::string otherUniqueTag = otherMember.getUniqueTag();
  if (myUniqueTag.empty() && otherUniqueTag.empty()) {
    if (vmViewId_ < otherMember.vmViewId_) {
      return -1;
    } else if (vmViewId_ > otherMember.vmViewId_) {
      return 1;
    }  // else they're the same, so continue
  } else if (myUniqueTag.empty()) {
    return -1;
  } else if (otherUniqueTag.empty()) {
    return 1;
  } else {
    int i = myUniqueTag.compare(otherUniqueTag);
    if (i != 0) {
      return i;
    }
  }
  return 0;
}

void ClientProxyMembershipID::readVersion(int flags, DataInput& input) {
  if (flags & ClientProxyMembershipID::VERSION_MASK) {
    const auto version = Version::read(input);
    LOGDEBUG("ClientProxyMembershipID::readVersion ordinal = %d ",
             version.getOrdinal());
  }
}

}  // namespace client
}  // namespace geode
}  // namespace apache
