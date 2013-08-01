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

/**
 * @file solver_world_connection.hpp
 * This file defines a class that simplifies connecting to the world model
 * as a solver, maintaining thread safety with messages, and sending keep
 * alive packets to maintain a solver-world model connection.
 *
 * @author Bernhard Firner
 */

#ifndef __SOLVER_WORLD_CONNECTION_HPP__
#define __SOLVER_WORLD_CONNECTION_HPP__

//Owl libcpp includes
#include <owl/message_receiver.hpp>
#include <owl/simple_sockets.hpp>
#include <owl/world_model_protocol.hpp>

#include <map>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <regex.h>

/**
 * Connection from a solver to the world model.
 */
class SolverWorldModel {
  public:
    ///An attribute update
    struct AttrUpdate {
      std::u16string type;
      world_model::grail_time time;
      world_model::URI target;
      std::vector<uint8_t> data;
    };

  private:
    ///On-demand requests from clients. These are forwarded from the world model
    struct OnDemandArgs {
      std::u16string request;
      regex_t exp;
      bool valid;
      bool operator<(const OnDemandArgs& other) const {
        return request < other.request;
      }
    };

    ///Send a handshake and a type declaration message.
    bool reconnect();

    /*
     * Send a message with automatic retries when disconnected.
     * First retry is immediate, the next is after 1 second,
     * and then retries are every 8 seconds.
     * Will block.
     */
    void sendAndReconnect(const std::vector<unsigned char>& buff);

    ///Lock for thread and variables to monitor on-demand request status.
    std::mutex trans_mutex;
    /**
     * A map from type aliases to a set of requests, remembering if these types
     * should be sent.
     * */
    std::map<uint32_t, std::multiset<OnDemandArgs>> on_demand_on;
    ///Thread that runs the trackOnDemands process
    std::thread on_demand_tracker;
    ///True when threaded operations should stop
    bool interrupted;
    ///True when threaded trackOnDemands process is running
    bool running;

    /**
     * A function to run in its own thread that handles messages from the
     * world model. This handles the messages "start_on_demand,"
     * "stop_on_demand," and "keep_alive"
     * */
    void trackOnDemands();

    std::vector<world_model::solver::AliasType> types;
    std::map<std::u16string, uint32_t> aliases;

    ///This solver's origin string
    std::u16string origin;

    ClientSocket s;
    MessageReceiver ss;
    std::string ip;
    uint16_t port;

    /**
     * Mutex that should be locked before sending data from the socket.
     * This keeps the SolverConnection class thread safe.
     */
    std::mutex send_mutex;
  public:

    /*
     * Connect to the world model at the given ip address and port and
     * immediately announce the provided types.
     * OnDemand types are indicated with a true boolean value in their pairs.
     */
    SolverWorldModel(std::string ip, uint16_t port, std::vector<std::pair<std::u16string, bool>>& types, std::u16string origin);

    ~SolverWorldModel();

    /*
     * Return the status of the connection.
     */
    bool connected();

		/*
		 * Register new solution types.
		 */
		void addTypes(std::vector<std::pair<std::u16string, bool>>& new_types);

    /*
     * Send new data to the world model.
     * If create_uris is true then any URIs that are named as targets but that
     * do not exist in the world model will be created so that these
     * solutions can be pushed. If create_uris is false then those
     * solutions will not be pushed.
     */
    void sendData(std::vector<AttrUpdate>& solution, bool create_uris = true);

    /*
     * Create a new URI in the world model.
     */
    void createURI(world_model::URI uri, world_model::grail_time created);

    /*
     * Expire a world_model::URI in the world model.
     */
    void expireURI(world_model::URI uri, world_model::grail_time expires);

    /*
     * Delete a URI from the world model.
     */
    void deleteURI(world_model::URI uri);

    /*
     * Expire an attribute from the world model.
     */
    void expireURIAttribute(world_model::URI uri, std::u16string name, world_model::grail_time expires);

    /*
     * Delete an attribute from the world model.
     */
    void deleteURIAttribute(world_model::URI uri, std::u16string name);
};

#endif

