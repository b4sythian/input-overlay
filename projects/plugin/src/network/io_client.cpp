/*************************************************************************
 * This file is part of input-overlay
 * github.con/univrsal/input-overlay
 * Copyright 2021 univrsal <uni@vrsal.de>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************/

#include "io_client.hpp"
#include "../util/log.h"
#include "websocket_server.hpp"
#include <keycodes.h>

namespace network {
io_client::io_client(const std::string &name, tcp_socket socket) : m_holder()
{
    m_name = name;
    m_socket = socket;
    m_valid = true;
}

io_client::~io_client()
{
    netlib_tcp_close(m_socket);
}

tcp_socket io_client::socket() const
{
    return m_socket;
}

const char *io_client::name() const
{
    return m_name.c_str();
}

input_data *io_client::get_data()
{
    return &m_holder;
}

bool io_client::read_event(buffer &buf, const message msg)
{
    auto flag = true;
    auto read_string = [](buffer &buf) {
        std::string result;
        auto *len = buf.read<uint16_t>();
        if (len) {
            result.reserve(*len);
            void *str = nullptr;
            buf.read(&str, result.length());
            if (str)
                result.insert(0, static_cast<char *>(str), *len);
        }
        return result;
    };

    if (msg == MSG_UIOHOOK_EVENT) {
        auto *event = buf.read<uiohook_event>();
        if (event) {
            m_holder.dispatch_uiohook_event(event);
            wss::dispatch_uiohook_event(event, m_name);
        } else {
            flag = false;
        }
    } else if (msg == MSG_GAMEPAD_EVENT) {
        flag = dispatch_gamepad_input(buf);
    } else if (msg == MSG_GAMEPAD_CONNECTED) {
        auto* index = buf.read<uint8_t>();
        auto name = read_string(buf);

        if (!index) {
            flag = false;
            berr("Couldn't read gamepad device index");
        } else if (get_pad(name)) {
            berr("'%s' already exists on '%s'", name.c_str(), m_name.c_str());
            flag = false;
        } else {
            binfo("'%s' (id %i) connected to '%s'", name.c_str(), *index, m_name.c_str());
            auto new_pad = std::make_shared<gamepad::device>();
            new_pad->set_index(*index);
            new_pad->set_id(name);
            m_gamepads[*index] = new_pad;
            wss::dispatch_gamepad_event(new_pad, WSS_PAD_CONNECTED, m_name);
        }
    } else if (msg == MSG_GAMEPAD_RECONNECTED) {
        auto *index = buf.read<uint8_t>();
        auto name = read_string(buf);
        if (index) {
            auto pad = get_pad(name);
            if (pad) {
                // We just keep devices in the list so we don't have to do anything here
                binfo("'%s' (id %i) reconnected to '%s'", name.c_str(), *index, m_name.c_str());
                wss::dispatch_gamepad_event(pad, WSS_PAD_CONNECTED, m_name);
            } else {
                berr("Received reconnect event from '%s' with invalid gamepad name '%s' (id %i)", m_name.c_str(),
                     name.c_str(), *index);
            }
        } else {
            flag = false;
            berr("Couldn't read gamepad device index");
        }
    } else if (msg == MSG_GAMEPAD_DISCONNECTED) {
        auto *index = buf.read<uint8_t>();
        auto name = read_string(buf);
        if (index) {
            auto pad = get_pad(name);
            if (pad) {
                // We just keep devices in the list so we don't have to do anything here
                binfo("'%s' (id %i) disconnected from '%s'", name.c_str(), *index, m_name.c_str());
                wss::dispatch_gamepad_event(pad, WSS_PAD_DISCONNECTED, m_name);
            } else {
                berr("Received disconnect event from '%s' with invalid gamepad name '%s' (id %i)", m_name.c_str(),
                     name.c_str(), *index);
            }
        } else {
            flag = false;
            berr("Couldn't read gamepad device index");
        }
    }

    if (!flag)
        berr("Couldn't read event for client %s. Error: %s", name(), netlib_get_error());

    return flag;
}

bool io_client::valid() const
{
    return m_valid;
}

void io_client::mark_invalid()
{
    m_valid = false;
}

std::shared_ptr<gamepad::device> io_client::get_pad(const std::string &id)
{
    for (auto &pad : m_gamepads) {
        if (pad.second->get_id() == id)
            return pad.second;
    }
    return nullptr;
}

bool io_client::dispatch_gamepad_input(buffer &buf)
{
    auto *index = buf.read<uint8_t>();
    if (!index) {
        berr("Failed to read gamepad index");
        return false;
    }
    auto pad = m_gamepads.find(*index);

    if (pad == m_gamepads.end()) {
        berr("'%s' received gamepad input events for non existing gamepad (id %i)", m_name.c_str(), *index);
        return false;
    } else {
        gamepad::input_event *output;
        auto *is_axis = buf.read<uint8_t>();
        auto *vc = buf.read<uint16_t>();
        auto *vv = buf.read<float>();
        auto *time = buf.read<uint64_t>();

        if (is_axis && vc && vv && time) {
            if (*is_axis)
                output = pad->second->last_axis_event();
            else
                output = pad->second->last_button_event();
            output->vc = *vc;
            output->virtual_value = *vv;
            output->time = *time;
            wss::dispatch_gamepad_event(output, pad->second, *is_axis, m_name);
        } else {
            berr("Couldn't read gamepad event body.");
            return false;
        }
    }
    return true;
}
}
