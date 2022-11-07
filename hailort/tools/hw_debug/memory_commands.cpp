/**
 * @file memory_commands.cpp
 * @brief Commands to access (read/write) some memory (for example - channel registers, descriptors, physical, etc.)
 */
#include "memory_commands.hpp"

#include <regex>
#include <cassert>

Field::Field(const std::string &name) :
    m_name(name)
{}


std::string Field::name() const
{
    return m_name;
}

const std::map<std::string, std::shared_ptr<Field>> &MemorySource::get_fields() const
{
    return m_fields;
}

void MemorySource::add_field(std::shared_ptr<Field> field)
{
    assert(m_fields.find(field->name()) == m_fields.end());
    m_fields[field->name()] = field;
}

PrintCommand::PrintCommand(std::shared_ptr<MemorySource> memory) :
    ShellCommand("print", "p", get_help(memory->get_fields())),
    m_memory(memory)
{}

ShellResult PrintCommand::execute(const std::vector<std::string> &args)
{
    if (args.size() != 1) {
        return ShellResult("Invalid params\n");
    }

    std::string field_name{};
    size_t index{};
    std::tie(field_name, index) = parse_field(args[0]);

    const auto &fields = m_memory->get_fields();
    auto field_it = fields.find(field_name);
    if (fields.end() == field_it) {
        throw std::runtime_error(fmt::format("Field {} does not exist", field_name));
    }

    const auto &field = field_it->second;
    if (index >= field->elements_count()) {
        throw std::runtime_error(fmt::format("Index {} is out of range (max {})", index, field->elements_count()));
    }
    return ShellResult(field->print_element(*m_memory, index));
}

std::pair<std::string, size_t> PrintCommand::parse_field(const std::string &field_arg)
{
    static const std::regex pattern("([a-zA-Z]+)\\[([0-9]+)\\]");
    std::smatch match;
    if (std::regex_match(field_arg, match, pattern)) {
        assert(match.size() == 3);
        const auto &field = match[1];
        const auto index = std::atoi(match[2].str().c_str());
        return std::make_pair(field, index);
    }
    else {
        throw std::runtime_error(fmt::format("Invalid syntax {}", field_arg));
    }
}

std::string PrintCommand::get_help(const std::map<std::string, std::shared_ptr<Field>> &fields)
{
    std::string help = "Pretty print some field, usage: print <field-name>[<index>]. Fields: {";

    size_t index = 0;
    for (auto field : fields) {
        help += field.first;
        if (index != fields.size() - 1) {
            help += ",";
        }
        index++;
    }
    help += "}.";
    return help;
}
