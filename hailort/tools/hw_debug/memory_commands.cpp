/**
 * @file memory_commands.cpp
 * @brief Commands to access (read/write) some memory (for example - channel registers, descriptors, physical, etc.)
 */
#include "memory_commands.hpp"

#include <regex>
#include <cassert>

Field::Field(std::string &&name, std::string &&description) :
    m_name(std::move(name)),
    m_description(std::move(description))
{}


const std::string &Field::name() const
{
    return m_name;
}

const std::string &Field::description() const
{
    return m_description;
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

constexpr size_t PrintCommand::PRINT_ALL;

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

    if (index == PRINT_ALL) {
        std::vector<ShellResult> results;
        results.reserve(field->elements_count());
        for (size_t i = 0; i < field->elements_count(); i++) {
            results.emplace_back(ShellResult(field->print_element(*m_memory, i)));
        }
        return ShellResult(results);
    }
    else {
        if (index >= field->elements_count()) {
            throw std::runtime_error(fmt::format("Index {} is out of range (max {})", index, field->elements_count()));
        }
        return ShellResult(field->print_element(*m_memory, index));
    }
}

std::pair<std::string, size_t> PrintCommand::parse_field(const std::string &field_arg)
{
    static const std::regex field_name_pattern("([a-zA-Z]+)");
    static const std::regex array_access_pattern("([a-zA-Z]+)\\[([0-9]+)\\]");
    std::smatch match;

    if (std::regex_match(field_arg, match, field_name_pattern)) {
        assert(match.size() == 2);
        const auto field = match[1];
        return std::make_pair(field, PRINT_ALL);
    }
    else if (std::regex_match(field_arg, match, array_access_pattern)) {
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
    std::string help = "Pretty print some field, usage: print <field-name>[<index>]. Fields:\n";
    for (auto field : fields) {
        help += fmt::format("\t{} - {}\n", field.second->name(), field.second->description());
    }
    return help;
}
