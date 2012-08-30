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

#ifndef __CLIENT_WORLD_CONNECTION_HPP__
#define __CLIENT_WORLD_CONNECTION_HPP__

//Owl libcpp includes
#include <owl/message_receiver.hpp>
#include <owl/simple_sockets.hpp>
#include <owl/world_model_protocol.hpp>

#include <functional>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <queue>

//Forward declaration for Response and StepResponse
class ClientWorldConnection;

///Response of a client request to the world model
class Response {
  private:
    std::future<world_model::WorldState> data;
    uint64_t request_key;

    //The client world connection that is servicing the request
    ClientWorldConnection& cwc;

    Response& operator=(const Response&) = delete;
    Response(const Response&) = delete;
  public:
    /**
     * Create a new future response object for a request.
     */
    Response(std::future<world_model::WorldState>&& data, ClientWorldConnection& cwc, uint64_t key) : data(std::move(data)), cwc(cwc) {
      request_key = key;
    }

    ///Notify the ClientWorldConnection that this future is no longer required
    ~Response();

    /// Move constructor
    Response(Response&& other) : data(std::move(other.data)), cwc(other.cwc) {
      request_key = other.request_key;
    }

    /**
     * Get new data. Blocks if new data has not arrived yet.
     */
    world_model::WorldState get();

    ///True if a call to get() will not block
    bool ready();

    /**
     * Returns true if the request had an error and a call to get() would
     * throw an exception.
     */
    bool isError();

    /**
     * Returns any error associated with the request.
     */
    std::exception getError();
};

///Multipart response of a client streaming request to the world model
class StepResponse {
  private:
    std::future<world_model::WorldState> data;
    uint64_t request_key;

    //The client world connection that is servicing the request
    ClientWorldConnection& cwc;

    StepResponse& operator=(const StepResponse&) = delete;
    StepResponse(const StepResponse&) = delete;
  public:
    /**
     * Create a step response object to service streaming requests.
     */
    StepResponse(std::future<world_model::WorldState>&& data, ClientWorldConnection& cwc, uint64_t key) : data(std::move(data)), cwc(cwc) {
      request_key = key;
    }

    ///Notify the ClientWorldConnection that this future is no longer required
    ~StepResponse();

    /// Move constructor
    StepResponse(StepResponse&& other) : data(std::move(other.data)), cwc(other.cwc) {
      request_key = other.request_key;
    }

    ///Get the next data set or block until it is available and then return it.
    world_model::WorldState next();

    ///Returns true if a call to next() will not block. (TODO FIXME Returns true if there is no error -- documentation problem?)
    bool hasNext();

    /**
     * Returns true if the request had an error and a call to next() would
     * throw an exception.
     */
    bool isError();


    /**
     * Returns any error associated with the request.
     */
    std::exception getError();

    ///True if this streaming request is complete.
    bool isComplete();
};

/**
 * Connection to the world model from a client. Clients subscribe to
 * information about objects in the world model.
 * This class is thread safe.
 */
class ClientWorldConnection {
    //Allow Response and StepResponse to notify the ClientWorldConnection
    //if they are no longer required
    friend class Response;
    friend class StepResponse;
  protected:
    ///See if a request is still being serviced (only for StepResponse)
    bool isComplete(uint32_t key);
    /**
     * getNextFuture should only be called if hasNextFuture is true, otherwise
     * the future will be given an exception.
     * Streaming requests will have multiple futures while other
     * requests will only have a single future.
     */
    bool hasNextFuture(uint32_t key);
    ///Get the future for the request associated with @key
    std::future<world_model::WorldState> getNextFuture(uint32_t key);
    ///Check for an error with the request associated with @key
    bool hasError(uint32_t key);
    ///Get error (will return std::exception("No error") is there is none
    std::exception getError(uint32_t key);
    /**
     * Indicate to the client world model that it can delete any promises
     * associated with this request
     */
    void markFinished(uint32_t key);
  private:
    //Lock this before changing cur_key, errors, promises, or step_promises
    std::mutex promise_mutex;
    //TODO When gcc supports atomic_fast_uint32_t then switch to that
    //Remember which key is being assigned.
    uint32_t cur_key;
    //Map of errors for different response keys.
    std::map<uint64_t, std::exception> errors;
    //Promises to Responses and StepResponses
    //TODO FIXME The current versions of gcc have not implemented
    //the emplace operator for maps. I'm going to use pointers in here.
    std::map<uint64_t, std::queue<std::promise<world_model::WorldState>*>> step_promises;
    //Remember which promises are for non-StepResponses
    std::set<uint64_t> single_response;
    //Partial results that must be completed before fulfilling a promise
    std::map<uint64_t, world_model::WorldState> partial_results;

    void setError(uint32_t key, const std::string& error);
    std::future<world_model::WorldState> makePromise(uint32_t key);
    std::future<world_model::WorldState> makeStepPromise(uint32_t key);

    //This mutex should be locked before sending data out through the socket
    std::mutex out_mutex;
    ClientSocket s;
    MessageReceiver ss;
    std::string ip;
    uint16_t port;
    std::map<uint32_t, std::u16string> known_attributes;
    std::map<uint32_t, std::u16string> known_origins;
    bool interrupted;

    void receiveThread();
    std::thread rx_thread;

  public:

    /**
     * Connect to the world model.
     */
    ClientWorldConnection(std::string ip, uint16_t port);

    ~ClientWorldConnection();

    /**
     * Reconnect to the world model after losing or closing a connection.
     * Returns true upon connection success, false otherwise.
     */
    bool reconnect();

    /**
     * Returns information about the state of any URIs matching the
     * URI REGEX expression and any attributes matching any of the
     * REGEX expression in the attributes vector.
     * Request for the most recent state of the world model.
     */
    Response currentSnapshotRequest(const world_model::URI&, const std::vector<std::u16string>&);

    /**
     * Returns information about the state of any URIs matching the
     * URI REGEX expression and any attributes matching any of the
     * REGEX expression in the attributes vector.
     * Request for the state of the world model at the given end time,
     * using data starting at the given start time.
     */
    Response snapshotRequest(const world_model::client::Request& request);

    /**
     * Returns information about the state of any URIs matching the
     * URI REGEX expression and any attributes matching any of the
     * REGEX expression in the attributes vector.
     * Request all changes to the world model occuring between the
     * given start time and end time.
     */
    Response rangeRequest(const world_model::client::Request& request);

    /**
     * Returns information about the state of any URIs matching the
     * URI REGEX expression and any attributes matching any of the
     * REGEX expression in the attributes vector.
     * The current state of the URIs is returned and any new
     * updates are sent to the client as they arrive. Updates are
     * sent at the given interval - if any attribute has not changed
     * in the time interval then information about that attribute
     * is not resent.
     */
    StepResponse streamRequest(const world_model::URI&, const std::vector<std::u16string>&, uint64_t);

    /**
     * Returns true if this instance is connected to the world model,
     * false otherwise.
     */
    bool connected();
};

#endif

