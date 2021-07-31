// Copyright 2021 Factor Robotics
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "odrive_hardware_interface/odrive_hardware_interface.hpp"
#include "pluginlib/class_list_macros.hpp"

namespace odrive_hardware_interface
{
return_type ODriveHardwareInterface::configure(const hardware_interface::HardwareInfo& info)
{
  if (configure_default(info) != return_type::OK)
  {
    return return_type::ERROR;
  }

  hw_positions_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_velocities_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_efforts_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_commands_positions_.resize(info_.joints.size(), 0);
  hw_commands_velocities_.resize(info_.joints.size(), 0);
  hw_commands_efforts_.resize(info_.joints.size(), 0);
  control_level_.resize(info_.joints.size(), integration_level_t::VELOCITY);

  for (const hardware_interface::ComponentInfo& joint : info_.joints)
  {
    if (joint.command_interfaces.size() != 3)
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), "Joint '%s' has %d command interfaces. 3 expected.",
                   joint.name.c_str());
      return return_type::ERROR;
    }

    if (!(joint.command_interfaces[0].name == hardware_interface::HW_IF_POSITION ||
          joint.command_interfaces[0].name == hardware_interface::HW_IF_VELOCITY ||
          joint.command_interfaces[0].name == hardware_interface::HW_IF_EFFORT))
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"),
                   "Joint '%s' has %s command interface. Expected %s, %s, or %s.", joint.name.c_str(),
                   joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION,
                   hardware_interface::HW_IF_VELOCITY, hardware_interface::HW_IF_EFFORT);
      return return_type::ERROR;
    }

    if (joint.state_interfaces.size() != 3)
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), "Joint '%s'has %d state interfaces. 3 expected.",
                   joint.name.c_str());
      return return_type::ERROR;
    }

    if (!(joint.state_interfaces[0].name == hardware_interface::HW_IF_POSITION ||
          joint.state_interfaces[0].name == hardware_interface::HW_IF_VELOCITY ||
          joint.state_interfaces[0].name == hardware_interface::HW_IF_EFFORT))
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"),
                   "Joint '%s' has %s state interface. Expected %s, %s, or %s.", joint.name.c_str(),
                   joint.state_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION,
                   hardware_interface::HW_IF_VELOCITY, hardware_interface::HW_IF_EFFORT);
      return return_type::ERROR;
    }

    axis_.push_back(std::stoi(joint.parameters.at("axis")));
    KV_.push_back(std::stoi(joint.parameters.at("KV")));
  }

  odrive = new ODriveUSB();
  int result = odrive->init();
  if (result != 0)
  {
    RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
    return return_type::ERROR;
  }

  status_ = hardware_interface::status::CONFIGURED;
  return return_type::OK;
}

std::vector<hardware_interface::StateInterface> ODriveHardwareInterface::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> state_interfaces;
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_positions_[i]));
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &hw_velocities_[i]));
    state_interfaces.emplace_back(
        hardware_interface::StateInterface(info_.joints[i].name, hardware_interface::HW_IF_EFFORT, &hw_efforts_[i]));
  }

  return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> ODriveHardwareInterface::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> command_interfaces;
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_commands_positions_[i]));
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &hw_commands_velocities_[i]));
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_EFFORT, &hw_commands_efforts_[i]));
  }

  return command_interfaces;
}

return_type ODriveHardwareInterface::start()
{
  axis_requested_state_ = AXIS_STATE_CLOSED_LOOP_CONTROL;
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    int result = odrive->write(odrive->odrive_handle_, AXIS__REQUESTED_STATE + per_axis_offset * axis_[i],
                               axis_requested_state_);
    if (result != 0)
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
      return return_type::ERROR;
    }
  }

  status_ = hardware_interface::status::STARTED;
  return return_type::OK;
}

return_type ODriveHardwareInterface::stop()
{
  axis_requested_state_ = AXIS_STATE_IDLE;
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    int result = odrive->write(odrive->odrive_handle_, AXIS__REQUESTED_STATE + per_axis_offset * axis_[i],
                               axis_requested_state_);
    if (result != 0)
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
      return return_type::ERROR;
    }
  }

  status_ = hardware_interface::status::STOPPED;
  return return_type::OK;
}

return_type ODriveHardwareInterface::read()
{
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    int result;
    float Iq_measured, vel_estimate, pos_estimate;

    result = odrive->read(odrive->odrive_handle_,
                          AXIS__MOTOR__CURRENT_CONTROL__IQ_MEASURED + per_axis_offset * axis_[i], Iq_measured);
    if (result != 0)
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
      return return_type::ERROR;
    }
    else
    {
      hw_efforts_[i] = Iq_measured * 8.27 / KV_[i];
    }

    result =
        odrive->read(odrive->odrive_handle_, AXIS__ENCODER__VEL_ESTIMATE + per_axis_offset * axis_[i], vel_estimate);
    if (result != 0)
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
      return return_type::ERROR;
    }
    else
    {
      hw_velocities_[i] = vel_estimate * 2 * M_PI;
    }

    result =
        odrive->read(odrive->odrive_handle_, AXIS__ENCODER__POS_ESTIMATE + per_axis_offset * axis_[i], pos_estimate);
    if (result != 0)
    {
      RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
      return return_type::ERROR;
    }
    else
    {
      hw_positions_[i] = pos_estimate * 2 * M_PI;
    }
  }

  return return_type::OK;
}

return_type ODriveHardwareInterface::write()
{
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    int result;
    float Iq_setpoint, input_vel, input_pos;

    switch (control_level_[i])
    {
      case integration_level_t::UNDEFINED:
        RCLCPP_INFO(rclcpp::get_logger("ODriveHardwareInterface"), "Nothing is using the hardware interface!");
        return return_type::OK;
        break;

      case integration_level_t::EFFORT:
        Iq_setpoint = hw_commands_efforts_[i] / 8.27 * KV_[i];
        result = odrive->write(odrive->odrive_handle_,
                               AXIS__MOTOR__CURRENT_CONTROL__IQ_SETPOINT + per_axis_offset * axis_[i], Iq_setpoint);
        if (result != 0)
        {
          RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
          return return_type::ERROR;
        }
        break;

      case integration_level_t::VELOCITY:
        input_vel = hw_commands_velocities_[i] / 2 / M_PI;
        result =
            odrive->write(odrive->odrive_handle_, AXIS__CONTROLLER__INPUT_VEL + per_axis_offset * axis_[i], input_vel);
        if (result != 0)
        {
          RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
          return return_type::ERROR;
        }
        break;

      case integration_level_t::POSITION:
        input_pos = hw_commands_positions_[i] / 2 / M_PI;
        result =
            odrive->write(odrive->odrive_handle_, AXIS__CONTROLLER__INPUT_POS + per_axis_offset * axis_[i], input_pos);
        if (result != 0)
        {
          RCLCPP_ERROR(rclcpp::get_logger("ODriveHardwareInterface"), libusb_error_name(result));
          return return_type::ERROR;
        }
        break;
    }
  }

  return return_type::OK;
}
}  // namespace odrive_hardware_interface

PLUGINLIB_EXPORT_CLASS(odrive_hardware_interface::ODriveHardwareInterface, hardware_interface::SystemInterface)
