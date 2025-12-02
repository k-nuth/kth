// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONFIG_SCRIPT_HPP
#define KTH_CONFIG_SCRIPT_HPP

#include <iostream>
#include <string>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between base16/raw script and script_type.
 */
struct KD_API script {
    script() = default;

    /**
     * Initialization constructor.
     * @param[in]  mnemonic  The value to initialize with.
     */
    script(std::string const& mnemonic);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    script(chain::script const& value);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    script(data_chunk const& value);

    /**
     * Initialization constructor.
     * @param[in]  tokens  The mnemonic tokens to initialize with.
     */
    script(std::vector<std::string> const& tokens);

    /**
     * Copy constructor.
     * @param[in]  other  The object to copy into self on construct.
     */
    script(script const& x);

    /**
     * Serialize the script to bytes according to the wire protocol.
     * @return  The byte serialized copy of the script.
     */
    [[nodiscard]]
    kth::data_chunk to_data() const;

    /**
     * Return a pretty-printed copy of the script.
     * @return  A mnemonic-printed copy of the internal script.
     */
    [[nodiscard]]
    std::string to_string() const;

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    operator chain::script const&() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend std::istream& operator>>(std::istream& input,
                                    script& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend std::ostream& operator<<(std::ostream& output,
                                    script const& argument);

private:
    /**
     * The state of this object.
     */
    chain::script value_;
};

} // namespace kth::domain::config

#endif
