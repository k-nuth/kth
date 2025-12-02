#!/bin/bash

# East const conversion script
# Usage: ./scripts/east_const.sh
# This script is idempotent - running it multiple times produces no additional changes

set -e

# =============================================================================
# Types to convert
# =============================================================================
BASIC_TYPES="uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|size_t|bool|auto"
CLASS_TYPES="authority|byte_array|dictionary|mnemonic_result_list|one_byte|hd_chain_code|ek_salt|ec_compressed|dictionary_list|synchronizer_terminate|string_list|long_hash|hash|generator|error_category_impl|ek_entropy|ec_signature|metric_attribute|threading_model|message|get_data|fee_filter|binary|ek_seed|parse_encrypted_private|ec_uncompressed|recoverable_signature|quarter_hash|parse_encrypted_token|parse_encrypted_public|outs|ins|half_hash|endorsement|event_handler|address|reject|result_handler|not_found|header|transaction_entry|time_t|hash256|secp256k1_pubkey|arith_uint256|unspent_outputs|unspent_transaction|output_point|settings|short_hash|file_offset|heights|microseconds|input|output|transaction|points_value|wif_uncompressed|message_signature|block_const_ptr_list|settings_list|program|word_list|secp256k1_context"

ALL_TYPES="$BASIC_TYPES|$CLASS_TYPES"

# =============================================================================
# Find files (excluding external code)
# =============================================================================
FILES=$(find src -type f \( -name "*.hpp" -o -name "*.cpp" -o -name "*.ipp" \) \
  ! -path "*/secp256k1/*" \
  ! -path "*/consensus/*" \
  ! -path "*/external/*")

echo "Processing $(echo "$FILES" | wc -l | tr -d ' ') files..."

# =============================================================================
# Apply transformations to each file once
# =============================================================================
for file in $FILES; do
  sed -i '' -E "
    # Line start with spaces: const TYPE  -> TYPE const
    s/^([[:space:]]+)const ($ALL_TYPES) /\1\2 const /g

    # After ( : (const TYPE  -> (TYPE const
    s/\(const ($ALL_TYPES) /(\1 const /g

    # After , : , const TYPE  -> , TYPE const
    s/, const ($ALL_TYPES) /, \1 const /g

    # References: const TYPE& -> TYPE const&
    s/const ($ALL_TYPES)&/\1 const\&/g

    # References with space: const TYPE & -> TYPE const&
    s/const ($ALL_TYPES) &/\1 const\&/g

    # After ( with reference: (const TYPE& -> (TYPE const&
    s/\(const ($ALL_TYPES)&/(\1 const\&/g

    # After ( with reference and space: (const TYPE & -> (TYPE const&
    s/\(const ($ALL_TYPES) &/(\1 const\&/g

    # Templates: const TYPE<...>& -> TYPE<...> const&
    s/const ($ALL_TYPES)<([^>]+)>&/\1<\2> const\&/g

    # Templates with space: const TYPE<...> & -> TYPE<...> const&
    s/const ($ALL_TYPES)<([^>]+)> &/\1<\2> const\&/g

    # Namespaced templates: const NS::TYPE<...>& -> NS::TYPE<...> const&
    s/const (std::[a-zA-Z_]+)<([^>]+)>&/\1<\2> const\&/g

    # Namespaced templates with space: const NS::TYPE<...> & -> NS::TYPE<...> const&
    s/const (std::[a-zA-Z_]+)<([^>]+)> &/\1<\2> const\&/g

    # Pointers: const TYPE* -> TYPE const*
    s/const ($ALL_TYPES)\*/\1 const*/g

    # Pointers with space: const TYPE * -> TYPE const*
    s/const ($ALL_TYPES) \*/\1 const*/g

    # Reference format: const & -> const&
    s/const &/const\& /g

    # Fix double space after const&
    s/const&  /const\& /g
  " "$file"
done

echo "Done - east const conversion complete"
