// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_PRINTER_HPP
#define KTH_INFRASTUCTURE_CONFIG_PRINTER_HPP

#include <iostream>
#include <string>
#include <vector>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

// #define FMT_HEADER_ONLY 1
// #include <fmt/core.h>

#include <kth/infrastructure/config/parameter.hpp>
#include <kth/infrastructure/define.hpp>

namespace kth::infrastructure::config {

/**
 * Shorthand for property declarations in printer class.
 */
#define KI_PROPERTY_GET_REF(type, name) \
    public: type& get_##name() { return name##_; } \
    private: type name##_

/**
 * Class for managing the serialization of command line options and arguments.
 */
struct KI_API printer {

    /**
     * Number of arguments above which the argument is considered unlimited.
     */
    KI_API static int const max_arguments;

    /**
     * Construct an instance of the printer class.
     * @param[in]  settings     Populated config file settings metadata.
     * @param[in]  application  This application (e.g. 'bitcoin_server').
     * @param[in]  description  This application description (e.g. 'Server').
     */
    printer(const boost::program_options::options_description& settings,
        std::string const& application, std::string const& description="");

    /**
     * Construct an instance of the printer class.
     * @param[in]  options      Populated command line options metadata.
     * @param[in]  arguments    Populated command line arguments metadata.
     * @param[in]  application  This application (e.g. 'bx').
     * @param[in]  description  This command description (e.g. 'Convert BTC').
     * @param[in]  command      This command (e.g. 'btc').
     */
    printer(const boost::program_options::options_description& options,
        const boost::program_options::positional_options_description& arguments,
        std::string const& application, std::string const& description="",
        std::string const& command="");

    /**
     * Convert a paragraph of text into a column.
     * This formats to 80 char width as: [ 23 | ' ' | 55 | '\n' ].
     * If one word exceeds width it will cause a column overflow.
     * This always sets at least one line and always collapses whitespace.
     * @param[in]  paragraph  The paragraph to columnize.
     * @return                The column, as a list of fragments.
     */
    std::vector<std::string> columnize(std::string const& paragraph, size_t width);

    /**
     * Format the command description.
     * @return  The command description.
     */
    std::string format_description();

    /**
     * Format the parameters table.
     * @param[in]  positional  True for positional otherwize named.
     * @return                 The formatted help arguments table.
     */
    std::string format_parameters_table(bool positional);

    /**
     * Format the settings table.
     * @return  The formatted settings table.
     */
    std::string format_settings_table();

    /**
     * Format a paragraph.
     * @param[in]  paragraph  The text to format.
     * @return                The formatted paragraph.
     */
    std::string format_paragraph(std::string const& paragraph);

    /**
     * Format the command line usage.
     * @return  The formatted usage.
     */
    std::string format_usage();

    /**
     * Format the command line parameters.
     * @return  The formatted command line parameters.
     */
    std::string format_usage_parameters();

    /**
     * Build the list of argument name/count tuples.
     */
    void generate_argument_names();

    /**
     * Build the list of parameters.
     */
    void generate_parameters();

    /**
     * Parse the arguments and options into the normalized parameter list.
     */
    void initialize();

    /**
     * Serialize command line help (full details).
     * @param[out] output  Stream that is sink for output.
     */
    void commandline(std::ostream& output);

    /**
     * Serialize as config settings (full details).
     * @param[out] output  Stream that is sink for output.
     */
    void settings(std::ostream& output);

    /**
     * Virtual property declarations, passed on construct.
     */
    KI_PROPERTY_GET_REF(boost::program_options::options_description, options);
    KI_PROPERTY_GET_REF(boost::program_options::positional_options_description, arguments);
    KI_PROPERTY_GET_REF(std::string, application);
    KI_PROPERTY_GET_REF(std::string, description);
    KI_PROPERTY_GET_REF(std::string, command);

    /**
     * Virtual property declarations, generated from metadata.
     */
    KI_PROPERTY_GET_REF(argument_list, argument_names);
    KI_PROPERTY_GET_REF(parameter_list, parameters);
};

#undef PROPERTY_GET_REF

} // namespace kth::infrastructure::config

#endif
