/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailortcli.hpp
 * @brief HailoRT CLI.
 **/

#ifndef _HAILO_HAILORTCLI_HPP_
#define _HAILO_HAILORTCLI_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/device.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "CLI/CLI.hpp"
#include <string>

using namespace hailort;

#define PARSE_CHECK(cond, message) \
    do {                                                                        \
        if (!(cond)) {                                                          \
            throw CLI::ParseError(message, CLI::ExitCodes::InvalidError);       \
        }                                                                       \
    } while (0)

struct hailo_pcie_params {
    std::string pcie_bdf;  // if empty use the first scanned
};

struct hailo_eth_params {
    std::string ip_addr;
};

struct hailo_vdevice_params {
    uint32_t device_count;
};

enum class DeviceType {
    PCIE = 0,
    ETH,
    DEFAULT
};

struct hailo_device_params {
    DeviceType device_type;
    hailo_pcie_params pcie_params;
    hailo_eth_params eth_params;
    hailo_vdevice_params vdevice_params;
};

void add_device_options(CLI::App *app, hailo_device_params &device_params);
void add_vdevice_options(CLI::App *app, hailo_device_params &device_params);
Expected<std::unique_ptr<Device>> create_device(const hailo_device_params &device_params);
Expected<std::unique_ptr<Device>> create_pcie_device(const hailo_pcie_params &pcie_params);

/**
 * CLI11 transformer object, converting enum argument from string.
 * Use this object instead of CLI::CheckedTransformer in order
 * to avoid ugly prints in the help message.
 */
template<typename EnumType>
class HailoCheckedTransformer : public CLI::CheckedTransformer
{
public:
    HailoCheckedTransformer(std::vector<std::pair<std::string, EnumType>> values) :
        CLI::CheckedTransformer(values)
    {
        desc_function_ = [values]() {
            return CLI::detail::generate_map(CLI::detail::smart_deref(values), true);
        };
    }
};

class DeprecationAction
{
public:
    DeprecationAction() = default;
    virtual ~DeprecationAction() = default;

    virtual std::string deprecate(bool message_inline) = 0;
    static std::string get_inline_description(CLI::Option *opt, const std::string &message)
    {
        const auto orig_desc = opt->get_description();
        std::stringstream new_desc;
        if (!orig_desc.empty()) {
            new_desc << orig_desc;
            if (orig_desc.back() != '\n') {
                new_desc << std::endl;
            }
        }
        new_desc << "Note: " << message;
        return new_desc.str();
    }
};
using DeprecationActionPtr = std::shared_ptr<DeprecationAction>;

class OptionDeprecation : public DeprecationAction
{
public:
    OptionDeprecation(CLI::Option *opt, const std::string &replacement) :
        DeprecationAction(),
        m_opt(opt),
        m_replacement(replacement)
    {
        assert(nullptr != opt);
    }

    virtual ~OptionDeprecation() = default;

    // Based off of CLI::deprecate_option, changed logic to suit our needs
    virtual std::string deprecate(bool message_inline) override
    {
        std::stringstream message;
        message << "'" << m_opt->get_name() << "' is deprecated, please use '" << m_replacement << "' instead." << std::endl;
        CLI::Validator deprecate_warning(
            [message = message.str()](std::string &) {
                std::cout << message;
                return std::string();
            }, message_inline ? "" : "DEPRECATED");
        deprecate_warning.application_index(0);
        if (message_inline) {
            m_opt->description(get_inline_description(m_opt, message.str()));
        }
        m_opt->check(deprecate_warning);
        return message.str();
    }

private:
    CLI::Option *const m_opt;
    const std::string m_replacement;
};

class ValueDeprecation : public DeprecationAction
{
public:
    ValueDeprecation(CLI::Option *opt, const std::string &value, const std::string &replacement) :
        DeprecationAction(),
        m_opt(opt),
        m_value(value),
        m_replacement(replacement)
    {
        assert(nullptr != opt);
    }
    
    virtual ~ValueDeprecation() = default;

    // Based off of CLI::deprecate_option, changed logic to suit our needs
    virtual std::string deprecate(bool message_inline) override
    {
        std::stringstream message;
        message << "'" << m_value << "' is deprecated, please use '" << m_replacement << "' instead." << std::endl;
        // We capture the members by value (i.e. copy), since the Validator can outlive this object
        CLI::Validator deprecate_warning(
            [message = message.str(), opt = m_opt, value = m_value](std::string &) {
                const auto results = opt->results();
                if ((results.size() == 1) && (results[0] == value)) {
                    std::cout << message;
                }
                return std::string();
            }, "");
        deprecate_warning.application_index(0);
        if (message_inline) {
            m_opt->description(get_inline_description(m_opt, message.str()));
        }
        // Hack: transform() and not check(), because we want the string values of opt->results() and not the enum values after HailoCheckedTransformer
        // transform places the Validator at the head of the validators in opt, so we'll get this check before the transformation is done
        m_opt->transform(deprecate_warning);
        return message.str();
    }

private:
    CLI::Option *const m_opt;
    const std::string m_value;
    const std::string m_replacement;
};

inline void hailo_deprecate_options(CLI::App *app, const std::vector<DeprecationActionPtr> &actions, bool set_footer = true)
{
    // std::set and not std::vector in case two actions have the smae deprection string
    std::set<std::string> deprecation_messages;
    for (const auto& deprecation_action : actions) {
        deprecation_messages.insert(deprecation_action->deprecate(!set_footer));
    }

    if (set_footer) {
        std::stringstream footer_message;
        footer_message << "Deprecated flags/options:" << std::endl;
        for (const auto &message : deprecation_messages) {
            footer_message << " * " << message;
            if (message.back() != '\n') {
                footer_message << std::endl;
            }
        }
        app->footer(footer_message.str());
    }
}

#endif /* _HAILO_HAILORTCLI_HPP_ */