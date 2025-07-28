# Clang-Format Notes

## Leading Commas in Enums

Clang-format doesn't natively support leading commas. For enums with this style, you can:

### Option 1: Disable formatting for specific blocks
```cpp
enum class verify_result_type : uint8_t {
    // clang-format off
    verify_result_eval_false = 0
    , verify_result_op_return
    , verify_result_input_sigchecks
    , verify_result_invalid_operand_size
    , verify_result_invalid_number_range
    // clang-format on
};
```

### Option 2: Use trailing commas (more standard)
```cpp
enum class verify_result_type : uint8_t {
    verify_result_eval_false = 0,
    verify_result_op_return,
    verify_result_input_sigchecks,
    verify_result_invalid_operand_size,
    verify_result_invalid_number_range,
};
```

### Option 3: Apply fixes selectively
Run the formatter but manually revert enum changes:
```bash
./scripts/check-format.sh --fix
git add . -p  # Selectively stage changes, skip enum formatting
```

## Current Configuration

- `AllowShortEnumsOnASingleLine: false` - Forces multiline enums
- `Cpp11BracedListStyle: false` - Less aggressive list formatting
- `ColumnLimit: 0` - No line length restrictions
