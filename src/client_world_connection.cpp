/*
 * Copyright (c) 2012 Bernhard Firner and Rutgers University
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 * or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

/*******************************************************************************
 * This file defines a class that simplifies connecting to the world model
 * as a client.
 ******************************************************************************/

#include "client_world_connection.hpp"

#include <owl/netbuffer.hpp>
#include <owl/simple_sockets.hpp>
#include <owl/world_model_protocol.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <future>
#include <chrono>

#include <iostream>

//Networking headers.
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace world_model;
using namespace std;

/*******************************************************************************
 * Functions for the Response class
 ******************************************************************************/
//Response& Response::operator=(Respons&& other) {
  //std::swap(future<world_model::WorldState>(std::move(other.data)), data);
  //request_key = other.request_key;
  //cwc = other.cwc;
  //return *this;
//}

Response::~Response() {
  //Indicate to the client world model that it can delete any promises
  //associated with this request
  cwc.markFinished(request_key);
}

world_model::WorldState Response::get() {
  if (isError()) {
    throw getError();
  }
  return data.get();
};

bool Response::ready() {
	if (not data.valid()) {
		return false;
	}
	else {
		//Check if the data is ready immediately
		return std::future_status::ready == (std::future_status)data.wait_for(std::chrono::seconds(0));
	}
}

bool Response::isError() {
  return cwc.hasError(request_key);
}

std::exception Response::getError() {
  return cwc.getError(request_key);
}

/*******************************************************************************
 * Functions for the StepResponse class
 ******************************************************************************/
world_model::WorldState StepResponse::next() {
  if (isError()) {
    throw getError();
  }
	//If a next future wasn't available after the last request then this is invalid
	if (data.valid()) {
		world_model::WorldState wd = data.get();
		//Update the future
		if (cwc.hasNextFuture(request_key)) {
			data = cwc.getNextFuture(request_key);
		}
		return wd;
	}
	else {
		throw std::logic_error("Next value requested for request without a valid request.");
	}
};

StepResponse::~StepResponse() {
  //Indicate to the client world model that it can delete any promises
  //associated with this request
  cwc.markFinished(request_key);
}

bool StepResponse::hasNext() {
	if (not data.valid()) {
		return false;
	}
	else {
		//Check if the data is ready immediately
		return std::future_status::ready == (std::future_status)data.wait_for(std::chrono::seconds(0));
	}
}

bool StepResponse::isError() {
  return cwc.hasError(request_key);
}

std::exception StepResponse::getError() {
  return cwc.getError(request_key);
}

bool StepResponse::isComplete() {
  return cwc.isComplete(request_key);
}


/*******************************************************************************
 * Functions for ClientWorldConnection
 ******************************************************************************/
//See if a request is still being serviced
bool ClientWorldConnection::isComplete(uint32_t key) {
  std::unique_lock<std::mutex> lck(promise_mutex);
  return (step_promises.end() == step_promises.find(key));
}

bool ClientWorldConnection::hasNextFuture(uint32_t key) {
  std::unique_lock<std::mutex> lck(promise_mutex);
  return step_promises.end() != step_promises.find(key);
}

std::future<world_model::WorldState> ClientWorldConnection::getNextFuture(uint32_t key) {
  if (not hasNextFuture(key)) {
    throw std::logic_error("No next value in request");
  }
  std::unique_lock<std::mutex> lck(promise_mutex);
  std::queue<promise<WorldState>*>& pq = step_promises.find(key)->second;
  //Remove the previous promise and return a future from the next one
  promise<WorldState>* prev = pq.front();
  pq.pop();
  delete prev;
  future<WorldState> f = pq.front()->get_future();
  ////Clear out these promises if the request is complete
  //if (pq.size() == 1) {
    //delete pq.front();
    //step_promises.erase(key);
  //}
  return std::move(f);
}

//Check for an error
bool ClientWorldConnection::hasError(uint32_t key) {
  std::unique_lock<std::mutex> lck(promise_mutex);
  return errors.find(key) != errors.end();
}

//Get error (will return std::exception("No error") is there is none
std::exception ClientWorldConnection::getError(uint32_t key) {
  if (not hasError(key))
    return std::runtime_error("no error but getError called");
  else {
    std::unique_lock<std::mutex> lck(promise_mutex);
    return errors[key];
  }
}

void ClientWorldConnection::markFinished(uint32_t key) {
  auto I = step_promises.find(key);
  if (I != step_promises.end()) {
    //Delete all of the promises in this object
    //Obviously the user should not delete the client world connection when
    //they have outstanding Response or StepResponse objects
    while (not I->second.empty()) {
      delete I->second.front();
      I->second.pop();
    }
    step_promises.erase(key);
  }
}


/**
 * Reconnect to the world model after losing or closing a connection.
 * Returns true upon connection success, false otherwise.
 */
bool ClientWorldConnection::reconnect() {
  //Make sure that the rx_thraed isn't running
  interrupted = true;
  try {
    rx_thread.join();
  }
  catch (std::exception& e) {
  }
  interrupted = false;

  //See if the socket ready but not connected
  if (not s) {
    //Otherwise try to make a new connection
    ClientSocket s2(AF_INET, SOCK_STREAM, 0, port, ip, SOCK_NONBLOCK);
    if (not s2) {
      std::cerr<<"Failed to connect to the GRAIL world model.\n";
      return false;
    }
    else {
      s = std::move(s2);
    }
  }

  //Try to get the handshake message
  {
    std::vector<unsigned char> handshake = world_model::client::makeHandshakeMsg();

    //Send the handshake message
    s.send(handshake);
    std::vector<unsigned char> raw_message(handshake.size());
    size_t length = s.receive(raw_message);

    //Check if the handshake message failed
    if (not (length == handshake.size() and
          std::equal(handshake.begin(), handshake.end(), raw_message.begin()) )) {
      std::cerr<<"Failure during client handshake with world model.\n";
      //Clear the socket by assigning a null socket
      s = std::move(ClientSocket(port, "", -1));
      return false;
    }
  }

  ss.previous_unfinished.clear();

  //Start a listening thread.
  std::cerr<<"Started receive thread\n";
  rx_thread = std::thread(&ClientWorldConnection::receiveThread, this);

  return true;
}

void ClientWorldConnection::receiveThread() {
  using namespace world_model::client;
  std::cerr<<"Starting client->wm receive thread.\n";
  try {
    while (not interrupted) {
      std::vector<unsigned char> raw_message = ss.getNextMessage(interrupted);
      //std::cerr<<"Got next message of length "<<raw_message.size()<<'\n';
      //size_t piece_length = readPrimitive<uint32_t>(raw_message, 0);
      if (interrupted) { break;}
      if (raw_message.size() < 5) {
        std::cerr<<"Received broken message from client world model.\n";
        break;
      }

      //Process this packet if it is valid
      if ( raw_message.size() >= 5 ) {
        //Handle the message according to its message type.
        client::MessageID message_type = (client::MessageID)raw_message[4];

        if ( MessageID::attribute_alias == message_type ) {
          std::vector<AliasType> aliases = decodeAttrAliasMsg(raw_message);
          for (auto alias = aliases.begin(); alias != aliases.end(); ++alias) {
            known_attributes[alias->alias] = alias->type;
          }
        }
        else if ( MessageID::origin_alias == message_type ) {
          std::vector<AliasType> aliases = decodeOriginAliasMsg(raw_message);
          for (auto alias = aliases.begin(); alias != aliases.end(); ++alias) {
            known_origins[alias->alias] = alias->type;
          }
        }
        else if ( MessageID::request_complete == message_type ) {
          uint32_t ticket = decodeRequestComplete(raw_message);
          std::unique_lock<std::mutex> lck(promise_mutex);
          if (step_promises.find(ticket) != step_promises.end()) {
            if (single_response.count(ticket) != 0) {
              auto I = step_promises.find(ticket);
              I->second.front()->set_value(partial_results[ticket]);
              partial_results.erase(ticket);
            }
            else {
              //Set the last value to an empty state
              step_promises.find(ticket)->second.back()->set_value(WorldState());
            }
          }
        }
        else if ( MessageID::data_response == message_type ) {
          //Convert the aliased world data to unaliased data for the user
          AliasedWorldData awd;
          uint32_t ticket;
          std::tie(awd, ticket) = decodeDataMessage(raw_message);
          WorldData wd;
          wd.object_uri = awd.object_uri;
          std::for_each(awd.attributes.begin(), awd.attributes.end(),
              [&](AliasedAttribute& attr) {
              wd.attributes.push_back(Attribute{known_attributes[attr.name_alias],
                attr.creation_date,
                attr.expiration_date,
                known_origins[attr.origin_alias],
                attr.data}); });
          //Now give this world data to the partial result if this is for a
          //Response, or give it directly to a StepResponse
          std::unique_lock<std::mutex> lck(promise_mutex);
          if (single_response.count(ticket) != 0) {
            partial_results[ticket][wd.object_uri] = wd.attributes;
          }
          else if (step_promises.find(ticket) != step_promises.end()) {
            //Set the value of the last entry and make a new promise for the next entry
            WorldState ws;
            ws[wd.object_uri] = wd.attributes;
            auto I = step_promises.find(ticket);
            I->second.back()->set_value(ws);
            I->second.push(new promise<WorldState>());
          }
        }
        else if ( MessageID::keep_alive == message_type) {
          //Send a keep alive message in reply to a keep alive from
          //the server. This makes sure that we are replying at less
          //than the sever's timeout period.
          std::unique_lock<std::mutex> lck(out_mutex);
          s.send(client::makeKeepAlive());
        }
      }
    }
  }
  //Catch a network error and mark all of the promises invalid
  catch (std::runtime_error& err) {
    std::unique_lock<std::mutex> lck(promise_mutex);
    for (auto& ticket_promise : step_promises) {
      //TODO FIXME Can't find make_exception_ptr anywhere so using throwing and catching to get a pointer
      try {
        throw std::runtime_error("Connection Closed");
      } catch (std::exception e) {
        ticket_promise.second.back()->set_exception(std::current_exception());
      }
    }
  }
}

ClientWorldConnection::ClientWorldConnection(std::string ip, uint16_t port) : s(AF_INET, SOCK_STREAM, 0, port, ip, SOCK_NONBLOCK), ss(s) {
  //Store these values so that we can reconnect later
  this->ip = ip;
  this->port = port;

  cur_key = 0;

  interrupted = false;
  reconnect();
}

ClientWorldConnection::~ClientWorldConnection() {
  interrupted = true;
  //TODO FIXME Having something that could possible throw an exception in a destructor is bad.
  rx_thread.join();
  //Delete all of the promises
  for (auto I = step_promises.begin(); I != step_promises.end(); ++I) {
    //TODO FIXME Can't find make_exception_ptr anywhere so I'm doing this.
    try {
      throw std::logic_error("World Model Connection object is being destroyed");
    } catch (std::exception e) {
      I->second.front()->set_exception(std::current_exception());
    }
    while (not I->second.empty()) {
      delete I->second.front();
      I->second.pop();
    }
  }
}

//bool is_key(std::pair<uint32_t, promise<WorldState>>& p, uint32_t key) {
  //return p.first == key;
//}

future<WorldState> ClientWorldConnection::makeStepPromise(uint32_t key) {
  std::unique_lock<std::mutex> lck(promise_mutex);
  step_promises[key] = std::queue<promise<WorldState>*>();
  step_promises[key].push(new promise<WorldState>());
  if (errors.find(key) != errors.end()) {
    errors.erase(key);
  }
  return step_promises[key].front()->get_future();
}

future<WorldState> ClientWorldConnection::makePromise(uint32_t key) {
  {
    std::unique_lock<std::mutex> lck(promise_mutex);
    single_response.insert(key);
  }
  return makeStepPromise(key);
}

void ClientWorldConnection::setError(uint32_t key, const std::string& error) {
  std::unique_lock<std::mutex> lck(promise_mutex);
  if (step_promises.end() != step_promises.find(key)) {
    //TODO FIXME Can't find make_exception_ptr anywhere so I'm doing this.
    try {
      throw std::logic_error(error);
    } catch (std::exception e) {
      step_promises[key].back()->set_exception(std::current_exception());
    }
    //step_promises[key].back()->set_exception(std::logic_error(error));
  }
  errors[key] = std::logic_error(error);
}

/**
 * Returns information about the state of any URIs matching the
 * URI GLOB expression and any attributes matching any of the
 * GLOB expression in the attributes vector.
 * Request for the state of the world model at the given end time,
 * using data starting at the given start time.
 */
Response ClientWorldConnection::currentSnapshotRequest(const URI& uri, const vector<u16string>& attributes) {
  return snapshotRequest(world_model::client::Request{uri, attributes, 0, 0});
}

Response ClientWorldConnection::snapshotRequest(const client::Request& request) {
  uint64_t ticket;
  {
    std::unique_lock<std::mutex> lck(promise_mutex);
    ticket = cur_key++;
  }
  Response r(makePromise(ticket), *this, ticket);
  std::unique_lock<std::mutex> lck(out_mutex);
  if (not s and not reconnect()) {
    setError(ticket, "not connected");
  }
  else {
    //Send the snapshot request and prepare a promise
    s.send(client::makeSnapshotRequest(request, ticket));
  }
  return r;
}

/**
 * Returns information about the state of any URIs matching the
 * URI GLOB expression and any attributes matching any of the
 * GLOB expression in the attributes vector.
 * Request all changes to the world model occuring between the
 * given start time and end time.
 */
Response ClientWorldConnection::rangeRequest(const client::Request& request) {
  uint64_t ticket;
  {
    std::unique_lock<std::mutex> lck(promise_mutex);
    ticket = cur_key++;
  }
  Response r(makePromise(ticket), *this, ticket);
  std::unique_lock<std::mutex> lck(out_mutex);
  if (not s and not reconnect()) {
    setError(ticket, "not connected");
  }
  else {
    //Send the snapshot request and prepare a promise
    s.send(client::makeRangeRequest(request, ticket));
  }
  return r;
}

/**
 * Returns information about the state of any URIs matching the
 * URI GLOB expression and any attributes matching any of the
 * GLOB expression in the attributes vector.
 * The current state of the URIs is returned and any new
 * updates are sent to the client as they arrive. Updates are
 * sent at the given interval - if any attribute has not changed
 * in the time interval then information about that attribute
 * is not resent.
 */
StepResponse ClientWorldConnection::streamRequest(const URI& uri, const vector<u16string>& attributes, uint64_t interval) {
  uint64_t ticket;
  {
    std::unique_lock<std::mutex> lck(promise_mutex);
    ticket = cur_key++;
  }
  StepResponse r(makeStepPromise(ticket), *this, ticket);
  std::unique_lock<std::mutex> lck(out_mutex);
  if (not s and not reconnect()) {
    setError(ticket, "not connected");
  }
  else {
    client::Request request;
    request.object_uri = uri;
    request.attributes = attributes;
    request.stop_period = interval;
    //Send the snapshot request and prepare a promise
    s.send(client::makeStreamRequest(request, ticket));
  }
  return r;
}

/**
 * Returns true if this instance is connected to the world model,
 * false otherwise.
 */
bool ClientWorldConnection::connected() {
  if (s) {
    return true;
  }
  else {
    return false;
  }
}


