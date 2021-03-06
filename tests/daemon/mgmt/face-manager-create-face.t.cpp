/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014-2016,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mgmt/face-manager.hpp"
#include "fw/face-table.hpp"
#include <ndn-cxx/mgmt/dispatcher.hpp>
#include <ndn-cxx/util/dummy-client-face.hpp>

#include <thread>
#include <boost/property_tree/info_parser.hpp>

#include "tests/test-common.hpp"
#include "tests/identity-management-fixture.hpp"

namespace nfd {
namespace tests {

BOOST_AUTO_TEST_SUITE(Mgmt)
BOOST_AUTO_TEST_SUITE(TestFaceManager)

BOOST_FIXTURE_TEST_SUITE(CreateFace, BaseFixture)

class FaceManagerNode
{
public:
  FaceManagerNode(ndn::KeyChain& keyChain, const std::string& port = "6363")
    : face(getGlobalIoService(), keyChain, {true, true})
    , dispatcher(face, keyChain, ndn::security::SigningInfo())
    , authenticator(CommandAuthenticator::create())
    , manager(faceTable, dispatcher, *authenticator)
  {
    dispatcher.addTopPrefix("/localhost/nfd");

    std::string basicConfig =
      "face_system\n"
      "{\n"
      "  tcp\n"
      "  {\n"
      "    port " + port + "\n"
      "  }\n"
      "  udp\n"
      "  {\n"
      "    port " + port + "\n"
      "    mcast no\n"
      "  }\n"
      "  ether\n"
      "  {\n"
      "    mcast no\n"
      "  }\n"
      "}\n"
      "authorizations\n"
      "{\n"
      "  authorize\n"
      "  {\n"
      "    certfile any\n"
      "    privileges\n"
      "    {\n"
      "      faces\n"
      "    }\n"
      "  }\n"
      "}\n"
      "\n";
    std::istringstream input(basicConfig);
    nfd::ConfigSection configSection;
    boost::property_tree::read_info(input, configSection);

    ConfigFile config;
    manager.setConfigFile(config);
    authenticator->setConfigFile(config);
    config.parse(configSection, false, "dummy-config");
  }

  void
  closeFaces()
  {
    std::vector<std::reference_wrapper<Face>> facesToClose;
    std::copy(faceTable.begin(), faceTable.end(), std::back_inserter(facesToClose));
    for (Face& face : facesToClose) {
      face.close();
    }
  }

public:
  FaceTable faceTable;
  ndn::util::DummyClientFace face;
  ndn::mgmt::Dispatcher dispatcher;
  shared_ptr<CommandAuthenticator> authenticator;
  FaceManager manager;
};

class FaceManagerFixture : public IdentityManagementTimeFixture
{
public:
  FaceManagerFixture()
    : node1(m_keyChain, "16363")
    , node2(m_keyChain, "26363")
  {
    advanceClocks(time::milliseconds(1), 5);
  }

  ~FaceManagerFixture()
  {
    // Explicitly closing faces is necessary. Otherwise, in a subsequent test case,
    // incoming packets may be delivered to an old socket from previous test cases.
    node1.closeFaces();
    node2.closeFaces();
    advanceClocks(time::milliseconds(1), 5);
  }

public:
  FaceManagerNode node1; // used to test FaceManager
  FaceManagerNode node2; // acts as a remote endpoint
};

class TcpFaceOnDemand
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("tcp4://127.0.0.1:26363")
      .setFacePersistency(ndn::nfd::FACE_PERSISTENCY_ON_DEMAND);
  }
};

class TcpFacePersistent
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("tcp4://127.0.0.1:26363")
      .setFacePersistency(ndn::nfd::FACE_PERSISTENCY_PERSISTENT);
  }
};

class TcpFacePermanent
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("tcp4://127.0.0.1:26363")
      .setFacePersistency(ndn::nfd::FACE_PERSISTENCY_PERMANENT);
  }
};

class UdpFaceOnDemand
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("udp4://127.0.0.1:26363")
      .setFacePersistency(ndn::nfd::FACE_PERSISTENCY_ON_DEMAND);
  }
};

class UdpFaceCannotConnect // face that will cause afterCreateFaceFailure to be invoked
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("udp4://0.0.0.0:16363"); // cannot connect to self
  }
};


class UdpFacePersistent
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("udp4://127.0.0.1:26363")
      .setFacePersistency(ndn::nfd::FACE_PERSISTENCY_PERSISTENT);
  }
};

class UdpFacePermanent
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("udp4://127.0.0.1:26363")
      .setFacePersistency(ndn::nfd::FACE_PERSISTENCY_PERMANENT);
  }
};

class Success
{
public:
  ControlResponse
  getExpected()
  {
    return ControlResponse()
      .setCode(200)
      .setText("OK");
  }
};

template<int CODE>
class Failure
{
public:
  ControlResponse
  getExpected()
  {
    return ControlResponse()
      .setCode(CODE)
      .setText("Error"); // error description should not be checked
  }
};

namespace mpl = boost::mpl;

// pairs of CreateCommand and Success status
typedef mpl::vector<mpl::pair<TcpFaceOnDemand, Failure<500>>,
                    mpl::pair<TcpFacePersistent, Success>,
                    mpl::pair<TcpFacePermanent, Failure<500>>,
                    mpl::pair<UdpFaceOnDemand, Failure<500>>,
                    mpl::pair<UdpFacePersistent, Success>,
                    mpl::pair<UdpFacePermanent, Success>,
                    mpl::pair<UdpFaceCannotConnect, Failure<408>>> Faces;

BOOST_FIXTURE_TEST_CASE_TEMPLATE(NewFace, T, Faces, FaceManagerFixture)
{
  typedef typename T::first FaceType;
  typedef typename T::second CreateResult;

  Name commandName("/localhost/nfd/faces");
  commandName.append("create");
  commandName.append(FaceType().getParameters().wireEncode());
  auto command = makeInterest(commandName);
  m_keyChain.sign(*command);

  bool hasCallbackFired = false;
  this->node1.face.onSendData.connect([this, command, &hasCallbackFired] (const Data& response) {
      if (!command->getName().isPrefixOf(response.getName())) {
        return;
      }

      ControlResponse actual(response.getContent().blockFromValue());
      ControlResponse expected(CreateResult().getExpected());
      BOOST_CHECK_EQUAL(expected.getCode(), actual.getCode());
      BOOST_TEST_MESSAGE(actual.getText());

      if (actual.getBody().hasWire()) {
        ControlParameters expectedParams(FaceType().getParameters());
        ControlParameters actualParams(actual.getBody());

        BOOST_CHECK_EQUAL(expectedParams.getUri(), actualParams.getUri());
        BOOST_CHECK_EQUAL(expectedParams.getFacePersistency(), actualParams.getFacePersistency());
      }
      hasCallbackFired = true;
    });

  this->node1.face.receive(*command);
  this->advanceClocks(time::milliseconds(1), 5);

  BOOST_CHECK(hasCallbackFired);
}


typedef mpl::vector<// mpl::pair<mpl::pair<TcpFacePersistent, TcpFacePermanent>, TcpFacePermanent>, // no need to check now
                    // mpl::pair<mpl::pair<TcpFacePermanent, TcpFacePersistent>, TcpFacePermanent>, // no need to check now
                    mpl::pair<mpl::pair<UdpFacePersistent, UdpFacePermanent>, UdpFacePermanent>,
                    mpl::pair<mpl::pair<UdpFacePermanent, UdpFacePersistent>, UdpFacePermanent>> FaceTransitions;


BOOST_FIXTURE_TEST_CASE_TEMPLATE(ExistingFace, T, FaceTransitions, FaceManagerFixture)
{
  typedef typename T::first::first FaceType1;
  typedef typename T::first::second FaceType2;
  typedef typename T::second FinalFaceType;

  {
    // create face

    Name commandName("/localhost/nfd/faces");
    commandName.append("create");
    commandName.append(FaceType1().getParameters().wireEncode());
    auto command = makeInterest(commandName);
    m_keyChain.sign(*command);

    this->node1.face.receive(*command);
    this->advanceClocks(time::milliseconds(1), 5);
  }

  //
  {
    // re-create face (= change face persistency)

    Name commandName("/localhost/nfd/faces");
    commandName.append("create");
    commandName.append(FaceType2().getParameters().wireEncode());
    auto command = makeInterest(commandName);
    m_keyChain.sign(*command);

    bool hasCallbackFired = false;
    this->node1.face.onSendData.connect([this, command, &hasCallbackFired] (const Data& response) {
        if (!command->getName().isPrefixOf(response.getName())) {
          return;
        }

        ControlResponse actual(response.getContent().blockFromValue());
        BOOST_REQUIRE_EQUAL(actual.getCode(), 200);

        ControlParameters expectedParams(FinalFaceType().getParameters());
        ControlParameters actualParams(actual.getBody());
        BOOST_CHECK_EQUAL(expectedParams.getFacePersistency(), actualParams.getFacePersistency());

        hasCallbackFired = true;
      });

    this->node1.face.receive(*command);
    this->advanceClocks(time::milliseconds(1), 5);

    BOOST_CHECK(hasCallbackFired);
  }
}


class UdpFace
{
public:
  ControlParameters
  getParameters()
  {
    return ControlParameters()
      .setUri("udp4://127.0.0.1:16363")
      .setFacePersistency(ndn::nfd::FACE_PERSISTENCY_PERSISTENT);
  }
};


// Note that the transitions from on-demand TcpFace are intentionally not tested.
// On-demand TcpFace has a remote endpoint with a randomized port number.  Normal face
// creation operations will not need to create a face toward a remote port not listened by
// a channel.

typedef mpl::vector<mpl::pair<UdpFace, UdpFacePersistent>,
                    mpl::pair<UdpFace, UdpFacePermanent>> OnDemandFaceTransitions;

// need a slightly different logic to test transitions from OnDemand state
BOOST_FIXTURE_TEST_CASE_TEMPLATE(ExistingFaceOnDemand, T, OnDemandFaceTransitions, FaceManagerFixture)
{
  typedef typename T::first  OtherNodeFace;
  typedef typename T::second FaceType;

  {
    // create on-demand face

    Name commandName("/localhost/nfd/faces");
    commandName.append("create");
    commandName.append(OtherNodeFace().getParameters().wireEncode());
    auto command = makeInterest(commandName);
    m_keyChain.sign(*command);

    ndn::util::signal::ScopedConnection connection =
      this->node2.face.onSendData.connect([this, command] (const Data& response) {
          if (!command->getName().isPrefixOf(response.getName())) {
            return;
          }

          ControlResponse controlResponse(response.getContent().blockFromValue());
          BOOST_REQUIRE_EQUAL(controlResponse.getText(), "OK");
          BOOST_REQUIRE_EQUAL(controlResponse.getCode(), 200);
          uint64_t faceId = ControlParameters(controlResponse.getBody()).getFaceId();
          auto face = this->node2.faceTable.get(static_cast<FaceId>(faceId));

          // to force creation of on-demand face
          auto dummyInterest = make_shared<Interest>("/hello/world");
          face->sendInterest(*dummyInterest);
        });

    this->node2.face.receive(*command);
    this->advanceClocks(time::milliseconds(1), 5); // let node2 process command and send Interest
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // allow wallclock time for socket IO
    this->advanceClocks(time::milliseconds(1), 5); // let node1 accept Interest and create on-demand face
  }

  // make sure there is on-demand face
  FaceUri onDemandFaceUri(FaceType().getParameters().getUri());
  const Face* foundFace = nullptr;
  for (const Face& face : this->node1.faceTable) {
    if (face.getRemoteUri() == onDemandFaceUri) {
      foundFace = &face;
      break;
    }
  }
  BOOST_REQUIRE_MESSAGE(foundFace != nullptr, "on-demand face is not created");

  //
  {
    // re-create face (= change face persistency)

    Name commandName("/localhost/nfd/faces");
    commandName.append("create");
    commandName.append(FaceType().getParameters().wireEncode());
    auto command = makeInterest(commandName);
    m_keyChain.sign(*command);

    bool hasCallbackFired = false;
    this->node1.face.onSendData.connect(
      [this, command, &hasCallbackFired, foundFace] (const Data& response) {
        if (!command->getName().isPrefixOf(response.getName())) {
          return;
        }

        ControlResponse actual(response.getContent().blockFromValue());
        BOOST_REQUIRE_EQUAL(actual.getCode(), 200);

        ControlParameters expectedParams(FaceType().getParameters());
        ControlParameters actualParams(actual.getBody());
        BOOST_CHECK_EQUAL(actualParams.getFacePersistency(), expectedParams.getFacePersistency());
        BOOST_CHECK_EQUAL(actualParams.getFaceId(), foundFace->getId());
        BOOST_CHECK_EQUAL(foundFace->getPersistency(), expectedParams.getFacePersistency());

        hasCallbackFired = true;
      });

    this->node1.face.receive(*command);
    this->advanceClocks(time::milliseconds(1), 5);

    BOOST_CHECK(hasCallbackFired);
  }
}

BOOST_AUTO_TEST_SUITE_END() // CreateFace
BOOST_AUTO_TEST_SUITE_END() // TestFaceManager
BOOST_AUTO_TEST_SUITE_END() // Mgmt

} // namespace tests
} // namespace nfd
