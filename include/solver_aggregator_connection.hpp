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
 * as a solver.
 ******************************************************************************/

#ifndef __SOLVER_AGGREGATOR_CONNECTION_HPP__
#define __SOLVER_AGGREGATOR_CONNECTION_HPP__

#include <functional>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include <owl/aggregator_solver_protocol.hpp>

/**
 * Maintain connections to multiple aggregators and allow the user to easily
 * update rules or add new rules.
 * This is threaded - all function calls return.
 * The packCallback will be protected by a mutex and need not be thread safe.
 */
class SolverAggregator {
  public:
    ///The ip address and port number of an aggregator
    struct NetTarget {
      std::string ip;
      uint16_t port;
    };
    /**
     * Variables that indicate the reason for interrupting a thread
     * close_connection indicates that the connection should close.
     * add_subscriptions indicates that new subscriptions need to be requested.
     */
    enum class interrupt_type : char { none = 0x00, close_connection = 0x01, add_subscriptions = 0x02};
  private:
    ///Aggregators that this class will try to connect to
    std::vector<NetTarget> servers;
    ///Callback for new data samples
    std::function<void (SampleData&)> packCallback;
    std::mutex callback_mutex;
    ///Threads for each of the aggregator connections
    std::vector<std::thread> server_threads;
    ///Control access to the subscription list.
    std::mutex sub_mutex;
    ///List of subscription rules
    std::vector<aggregator_solver::Subscription> subscriptions;
    /**
     * Indicates interruption of aggregator connection threads
     * Reading the value as an interrupt_type indicates the kind of interrupt
     * This value is left as a char so that it can be evaluated as a boolean
     * by the non-blocking socket libraries.
     */
    interrupt_type interrupted;
  public:
    /**
     * Create a connection from a list of servers and send new packets
     * to the packCallback as they arrive. Calls to the callback will
     * be protected by a mutex so the callback does not need to be thread safe.
     * \param subscription The intitial subscription to send to the aggregator.
     */
    SolverAggregator(const std::vector<NetTarget>& servers, std::function<void (SampleData&)> packCallback);

    ~SolverAggregator();

    ///Add a new subscription request to current connections.
    void addRules(aggregator_solver::Subscription subscription);

    ///Disconnect from all aggregators and reconnect with a new set of subscriptions.
    void updateRules(aggregator_solver::Subscription subscription);

    ///Disconnect from all aggregators.
    void disconnect(); 
};

#endif

