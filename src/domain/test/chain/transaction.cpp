// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: chain transaction tests

TEST_CASE("chain transaction  constructor 1  always  returns default initialized", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_valid());
}

constexpr auto tx0_inputs =
    "f08e44a96bfb5ae63eda1a6620adae37ee37ee4777fb0336e1bbbc4de65310fc"
    "010000006a473044022050d8368cacf9bf1b8fb1f7cfd9aff63294789eb17601"
    "39e7ef41f083726dadc4022067796354aba8f2e02363c5e510aa7e2830b11547"
    "2fb31de67d16972867f13945012103e589480b2f746381fca01a9b12c517b7a4"
    "82a203c8b2742985da0ac72cc078f2ffffffff"_base16;

constexpr auto tx0_inputs_last_output =
    "f0c9c467000000001976a914d9d78e26df4e4601cf9b26d09c7b280ee764469f88ac"_base16;

constexpr auto tx1 =
    "0100000001f08e44a96bfb5ae63eda1a6620adae37ee37ee4777fb0336e1bbbc"
    "4de65310fc010000006a473044022050d8368cacf9bf1b8fb1f7cfd9aff63294"
    "789eb1760139e7ef41f083726dadc4022067796354aba8f2e02363c5e510aa7e"
    "2830b115472fb31de67d16972867f13945012103e589480b2f746381fca01a9b"
    "12c517b7a482a203c8b2742985da0ac72cc078f2ffffffff02f0c9c467000000"
    "001976a914d9d78e26df4e4601cf9b26d09c7b280ee764469f88ac80c4600f00"
    "0000001976a9141ee32412020a324b93b1a1acfdfff6ab9ca8fac288ac000000"
    "00"_base16;

static auto const tx1_hash = "bf7c3f5a69a78edd81f3eff7e93a37fb2d7da394d48db4d85e7e5353b9b8e270"_hash;

constexpr auto tx3_wire_serialized =
    "010000000209e300a61db28e4fd3562aec52647646fc55aa3e3f7d824f20f451"
    "a45db8c958010000006a4730440220364484206d2d3977373a82135cbdb78f20"
    "0e2160ec2636c9f080424a61748d15022056c9729b9fbd5c04170a7bb63b1d1b"
    "02da183fa3605864666dba6e216c3ce9270121027d4b693a2851541b1e393732"
    "0c5e4173ea8ab3f152f7a7fa96dbb936d2cff73dffffffff1cbb3eb855334221"
    "0c67e27dab3c2e72a9c0937b20dc6fe4d08d209fc4c2f163000000006a473044"
    "02207bc1940e12ec94544b7080518f73840f9bd191bd5fcb6b00f69a57a58658"
    "33bc02201bd759d978305e4346b39a9ee8b38043888621748dd1f8ab822df542"
    "427e49d6012102a17da2659b6149fb281a675519b5fd64dd80699dccd509f76e"
    "655699f2f625efffffffff021dc05c00000000001976a914e785da41a84114af"
    "0762c5a6f9e5b78ff730581988ac70e0cf02000000001976a914607a10e5b5f5"
    "3610341db013e77ba7c317a10c9088ac00000000"_base16;

constexpr auto tx3_store_serialized_v3 =
    "02ffffffff1dc05c00000000001976a914e785da41a84114af0762c5a6f9e5b7"
    "8ff730581988acffffffff70e0cf02000000001976a914607a10e5b5f5361034"
    "1db013e77ba7c317a10c9088ac0209e300a61db28e4fd3562aec52647646fc55"
    "aa3e3f7d824f20f451a45db8c95801006a4730440220364484206d2d3977373a"
    "82135cbdb78f200e2160ec2636c9f080424a61748d15022056c9729b9fbd5c04"
    "170a7bb63b1d1b02da183fa3605864666dba6e216c3ce9270121027d4b693a28"
    "51541b1e3937320c5e4173ea8ab3f152f7a7fa96dbb936d2cff73dffffffff1c"
    "bb3eb8553342210c67e27dab3c2e72a9c0937b20dc6fe4d08d209fc4c2f16300"
    "006a47304402207bc1940e12ec94544b7080518f73840f9bd191bd5fcb6b00f6"
    "9a57a5865833bc02201bd759d978305e4346b39a9ee8b38043888621748dd1f8"
    "ab822df542427e49d6012102a17da2659b6149fb281a675519b5fd64dd80699d"
    "ccd509f76e655699f2f625efffffffff0001"_base16;

constexpr auto tx4 =
    "010000000364e62ad837f29617bafeae951776e7a6b3019b2da37827921548d1"
    "a5efcf9e5c010000006b48304502204df0dc9b7f61fbb2e4c8b0e09f3426d625"
    "a0191e56c48c338df3214555180eaf022100f21ac1f632201154f3c69e1eadb5"
    "9901a34c40f1127e96adc31fac6ae6b11fb4012103893d5a06201d5cf61400e9"
    "6fa4a7514fc12ab45166ace618d68b8066c9c585f9ffffffff54b755c39207d4"
    "43fd96a8d12c94446a1c6f66e39c95e894c23418d7501f681b010000006b4830"
    "4502203267910f55f2297360198fff57a3631be850965344370f732950b47795"
    "737875022100f7da90b82d24e6e957264b17d3e5042bab8946ee5fc676d15d91"
    "5da450151d36012103893d5a06201d5cf61400e96fa4a7514fc12ab45166ace6"
    "18d68b8066c9c585f9ffffffff0aa14d394a1f0eaf0c4496537f8ab9246d9663"
    "e26acb5f308fccc734b748cc9c010000006c493046022100d64ace8ec2d5feeb"
    "3e868e82b894202db8cb683c414d806b343d02b7ac679de7022100a2dcd39940"
    "dd28d4e22cce417a0829c1b516c471a3d64d11f2c5d754108bdc0b012103893d"
    "5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b8066c9c585f9ffff"
    "ffff02c0e1e400000000001976a914884c09d7e1f6420976c40e040c30b2b622"
    "10c3d488ac20300500000000001976a914905f933de850988603aafeeb2fd7fc"
    "e61e66fe5d88ac00000000"_base16;

static auto const tx4_hash = "8a6d9302fbe24f0ec756a94ecfc837eaffe16c43d1e68c62dfe980d99eea556f"_hash;

constexpr char tx4_text[] = 
    "Transaction:\n"
    "\tversion = 1\n"
    "\tlocktime = 0\n"
    "Inputs:\n"
    "\thash = 5c9ecfefa5d14815922778a32d9b01b3a6e7761795aefeba1796f237d82ae664\n"
    "\tindex = 1\n"
    "\t[304502204df0dc9b7f61fbb2e4c8b0e09f3426d625a0191e56c48c338df3214555180eaf022100f21ac1f632201154f3c69e1eadb59901a34c40f1127e96adc31fac6ae6b11fb401] [03893d5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b8066c9c585f9]\n"
    "\tsequence = 4294967295\n"
    "\thash = 1b681f50d71834c294e8959ce3666f1c6a44942cd1a896fd43d40792c355b754\n"
    "\tindex = 1\n"
    "\t[304502203267910f55f2297360198fff57a3631be850965344370f732950b47795737875022100f7da90b82d24e6e957264b17d3e5042bab8946ee5fc676d15d915da450151d3601] [03893d5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b8066c9c585f9]\n"
    "\tsequence = 4294967295\n"
    "\thash = 9ccc48b734c7cc8f305fcb6ae263966d24b98a7f5396440caf0e1f4a394da10a\n"
    "\tindex = 1\n"
    "\t[3046022100d64ace8ec2d5feeb3e868e82b894202db8cb683c414d806b343d02b7ac679de7022100a2dcd39940dd28d4e22cce417a0829c1b516c471a3d64d11f2c5d754108bdc0b01] [03893d5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b8066c9c585f9]\n"
    "\tsequence = 4294967295\n"
    "Outputs:\n"
    "\tvalue = 15000000\n"
    "\tdup hash160 [884c09d7e1f6420976c40e040c30b2b62210c3d4] equalverify checksig\n"
    "\tvalue = 340000\n"
    "\tdup hash160 [905f933de850988603aafeeb2fd7fce61e66fe5d] equalverify checksig\n\n";

constexpr auto tx5 =
    "01000000023562c207a2a505820324aa03b769ee9c04a221eff59fdab6d52c312544a"
    "c4b21020000006a473044022075d3dd4cd26137f50d1b8c18b5ecbd13b7309b801f62"
    "83ebb951b137972d6e5b02206776f5e3acb2d996a9553f2438a4d2566c1fd786d9075"
    "5a5bca023bd9ae3945b0121029caef1b63490b7deabc9547e3e5d8b13c004b4bfd04d"
    "fae270874d569e5b89a8ffffffff8593568e460593c3dd30a470977a14928be6a29c6"
    "14a644c531471a773a63601020000006a47304402201fd9ea7dc62628ea82ff7b38cc"
    "90b3f2aa8c9ae25aa575600de38c79eafc925602202ca57bcd29d38a3e6aebd6809f7"
    "be4379d86f173b2ad2d42892dcb1dccca14b60121029caef1b63490b7deabc9547e3e"
    "5d8b13c004b4bfd04dfae270874d569e5b89a8ffffffff01763d0300000000001976a"
    "914e0d40d609d0282cc97314e454d194f65c16c257888ac00000000"_base16;

constexpr auto tx6 =
    "010000000100000000000000000000000000000000000000000000000000000000000"
    "00000ffffffff23039992060481e1e157082800def50009dfdc102f42697446757279"
    "2f5345475749542f00000000015b382d4b000000001976a9148cf4f6175b2651dcdff"
    "0051970a917ea10189c2d88ac00000000"_base16;

constexpr auto tx7 =
    "0100000001b63634c25f23018c18cbb24ad503672fe7c5edc3fef193ec0f581dd"
    "b27d4e401490000006a47304402203b361bfb7e189c77379d6ffc90babe1b9658"
    "39d0b9b60966ade0c4b8de28385f022057432fe6f8f530c54d3513e41da6fb138"
    "fba2440c877cd2bfb0c94cdb5610fbe0121020d2d76d6db0d1c0bda17950f6468"
    "6e4bf42481337707e9a81bbe48458cfc8389ffffffff010000000000000000566"
    "a54e38193e381aee4b896e7958ce381afe4bb96e4babae381abe38288e381a3e3"
    "81a6e7ac91e9a194e38292e5a5aae3828fe3828ce3828be7bea9e58b99e38292e"
    "8a8ade38191e381a6e381afe38184e381aae3818400000000"_base16;

static auto const tx7_hash = "cb1e303db604f066225eb14d59d3f8d2231200817bc9d4610d2802586bd93f8a"_hash;

TEST_CASE("chain transaction  constructor 2  valid input  returns input initialized", "[chain transaction]") {
    uint32_t version = 2345u;
    uint32_t locktime = 4568656u;
    data_chunk data = to_chunk(tx0_inputs);
    byte_reader reader(data);
    auto result_exp = chain::input::from_data(reader, true);
    REQUIRE(result_exp);
    chain::input::list inputs { 
        std::move(*result_exp) 
    };

    data = to_chunk(tx0_inputs_last_output);
    reader = byte_reader(data);
    auto result_exp_out = chain::output::from_data(reader);
    REQUIRE(result_exp_out);
    chain::output::list outputs { 
        std::move(*result_exp_out) 
    };

    chain::transaction instance(version, locktime, inputs, outputs);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(locktime == instance.locktime());
    REQUIRE(inputs == instance.inputs());
    REQUIRE(outputs == instance.outputs());
}

TEST_CASE("chain transaction  constructor 3  valid input  returns input initialized", "[chain transaction]") {
    uint32_t version = 2345u;
    uint32_t locktime = 4568656u;
    
    // chain::input::list inputs;
    // inputs.emplace_back();
    // REQUIRE(entity_from_data(inputs.back(), to_chunk(tx0_inputs)));
    data_chunk data = to_chunk(tx0_inputs);
    byte_reader reader(data);
    auto result_exp = chain::input::from_data(reader, true);
    REQUIRE(result_exp);
    chain::input::list inputs { 
        std::move(*result_exp) 
    };

    data = to_chunk(tx0_inputs_last_output);
    reader = byte_reader(data);
    auto result_exp_out = chain::output::from_data(reader);
    REQUIRE(result_exp_out);
    chain::output::list outputs { 
        std::move(*result_exp_out) 
    };

    // These must be non-const.
    auto dup_inputs = inputs;
    auto dup_outputs = outputs;

    chain::transaction instance(version, locktime, std::move(dup_inputs), std::move(dup_outputs));
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(locktime == instance.locktime());
    REQUIRE(inputs == instance.inputs());
    REQUIRE(outputs == instance.outputs());
}

TEST_CASE("chain transaction  constructor 4  valid input  returns input initialized", "[chain transaction]") {
    auto const raw_tx = to_chunk(tx1);
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    auto const expected = std::move(*result);

    chain::transaction instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("chain transaction  constructor 5  valid input  returns input initialized", "[chain transaction]") {
    auto const raw_tx = to_chunk(tx1);

    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    auto const expected = std::move(*result);

    chain::transaction instance(std::move(expected));
    REQUIRE(instance.is_valid());
}

TEST_CASE("chain transaction  constructor 6  valid input  returns input initialized", "[chain transaction]") {
    auto const raw_tx = to_chunk(tx1);
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    auto const expected = std::move(*result);
    hash_digest const expected_hash = tx1_hash;

    chain::transaction instance(expected, expected_hash);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
    REQUIRE(expected_hash == instance.hash());
}

TEST_CASE("chain transaction  constructor 7  valid input  returns input initialized", "[chain transaction]") {
    auto const raw_tx = to_chunk(tx1);

    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    auto const expected = std::move(*result);
    hash_digest const expected_hash = tx1_hash;

    chain::transaction instance(std::move(expected), expected_hash);
    REQUIRE(instance.is_valid());
    REQUIRE(expected_hash == instance.hash());
}

TEST_CASE("chain transaction  is coinbase  empty inputs  returns false", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_coinbase());
}

TEST_CASE("chain transaction  is coinbase  one null input  returns true", "[chain transaction]") {
    static const chain::input::list inputs{
        {chain::point{null_hash, chain::point::null_index}, {}, 0}};

    chain::transaction instance;
    instance.set_inputs(inputs);
    REQUIRE(instance.is_coinbase());
}

TEST_CASE("chain transaction  is coinbase  one non null input  returns false", "[chain transaction]") {
    static const chain::input::list inputs{
        {chain::point{null_hash, 42}, {}, 0}};

    chain::transaction instance;
    instance.set_inputs(inputs);
    REQUIRE( ! instance.is_coinbase());
}

TEST_CASE("chain transaction  is coinbase  two inputs first null  returns false", "[chain transaction]") {
    static const chain::input::list inputs{
        {chain::point{null_hash, chain::point::null_index}, {}, 0},
        {chain::point{null_hash, 42}, {}, 0}};

    chain::transaction instance;
    instance.set_inputs(inputs);
    REQUIRE( ! instance.is_coinbase());
}

TEST_CASE("chain transaction  is null non coinbase  empty inputs  returns false", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_null_non_coinbase());
}

TEST_CASE("chain transaction  is null non coinbase  one null input  returns false", "[chain transaction]") {
    static const chain::input::list inputs{
        {chain::point{null_hash, chain::point::null_index}, {}, 0}};

    chain::transaction instance;
    instance.set_inputs(inputs);
    REQUIRE( ! instance.is_null_non_coinbase());
}

TEST_CASE("chain transaction  is null non coinbase  one non null input  returns false", "[chain transaction]") {
    static const chain::input::list inputs{
        {chain::point{null_hash, 42}, {}, 0}};

    chain::transaction instance;
    instance.set_inputs(inputs);
    REQUIRE( ! instance.is_null_non_coinbase());
}

TEST_CASE("chain transaction  is null non coinbase  two inputs first null  returns true", "[chain transaction]") {
    static const chain::input::list inputs{
        {chain::point{null_hash, chain::point::null_index}, {}, 0},
        {chain::point{null_hash, 42}, {}, 0}};

    chain::transaction instance;
    instance.set_inputs(inputs);
    REQUIRE(instance.is_null_non_coinbase());
}

TEST_CASE("chain transaction  is final  locktime zero  returns true", "[chain transaction]") {
    static size_t const height = 100;
    static uint32_t const time = 100;
    chain::transaction instance;
    instance.set_locktime(0);
    REQUIRE(instance.is_final(height, time));
}

TEST_CASE("chain transaction  is final  locktime less block time greater threshold  returns true", "[chain transaction]") {
    static size_t const height = locktime_threshold + 100;
    static uint32_t const time = 100;
    chain::transaction instance;
    instance.set_locktime(locktime_threshold + 50);
    REQUIRE(instance.is_final(height, time));
}

TEST_CASE("chain transaction  is final  locktime less block height less threshold returns true", "[chain transaction]") {
    static size_t const height = 100;
    static uint32_t const time = 100;
    chain::transaction instance;
    instance.set_locktime(50);
    REQUIRE(instance.is_final(height, time));
}

TEST_CASE("chain transaction  is final  locktime input not final  returns false", "[chain transaction]") {
    static size_t const height = 100;
    static uint32_t const time = 100;
    chain::input input;
    input.set_sequence(1);
    chain::transaction instance(0, 101, {input}, {});
    REQUIRE( ! instance.is_final(height, time));
}

TEST_CASE("chain transaction  is final  locktime inputs final  returns true", "[chain transaction]") {
    static size_t const height = 100;
    static uint32_t const time = 100;
    chain::input input;
    input.set_sequence(max_input_sequence);
    chain::transaction instance(0u, 101u, {input}, {});
    REQUIRE(instance.is_final(height, time));
}

TEST_CASE("chain transaction  is locked  version 1 empty  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.set_version(1);
    REQUIRE( ! instance.is_locked(0, 0));
}

TEST_CASE("chain transaction  is locked  version 2 empty  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.set_version(2);
    REQUIRE( ! instance.is_locked(0, 0));
}

TEST_CASE("chain transaction  is locked  version 1 one of two locked locked  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.set_inputs({{{}, {}, 1}, {{}, {}, 0}});
    instance.set_version(1);
    REQUIRE( ! instance.is_locked(0, 0));
}

TEST_CASE("chain transaction  is locked  version 4 one of two locked  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.set_inputs({{{}, {}, 1}, {{}, {}, 0}});
    instance.set_version(4);
    REQUIRE(instance.is_locked(0, 0));
}

TEST_CASE("chain transaction  is locktime conflict  locktime zero  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.set_locktime(0);
    REQUIRE( ! instance.is_locktime_conflict());
}

TEST_CASE("chain transaction  is locktime conflict  input sequence not maximum  returns false", "[chain transaction]") {
    chain::input input;
    input.set_sequence(1);
    chain::transaction instance(0, 2143u, {input}, {});
    REQUIRE( ! instance.is_locktime_conflict());
}

TEST_CASE("chain transaction  is locktime conflict  no inputs  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.set_locktime(2143);
    REQUIRE(instance.is_locktime_conflict());
}

TEST_CASE("chain transaction  is locktime conflict  input max sequence  returns true", "[chain transaction]") {
    // This must be non-const.
    chain::input::list inputs;

    inputs.emplace_back();
    inputs.back().set_sequence(max_input_sequence);
    chain::transaction instance(0, 2143u, std::move(inputs), {});
    REQUIRE(instance.is_locktime_conflict());
}

TEST_CASE("chain transaction from data insufficient version bytes  failure", "[chain transaction]") {
    data_chunk data(2);

    chain::transaction instance;

    byte_reader reader(data);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("chain transaction from data insufficient input bytes  failure", "[chain transaction]") {
    data_chunk data = to_chunk("0000000103"_base16);
    chain::transaction instance;
    byte_reader reader(data);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("chain transaction from data insufficient output bytes  failure", "[chain transaction]") {
    data_chunk data = to_chunk("000000010003"_base16);
    chain::transaction instance;
    byte_reader reader(data);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

// TODO(legacy): update test for v4 store serialization (input with witness).
// *******1
TEST_CASE("chain transaction from data compare wire to store  success", "[None]") {
   static auto const wire = true;
   auto const data_wire = to_chunk(tx3_wire_serialized);

    byte_reader reader(data_wire);
    auto result = chain::transaction::from_data(reader, wire);
    REQUIRE(result);
    auto const wire_tx = std::move(*result);
    REQUIRE(data_wire == wire_tx.to_data(wire));

    auto const get_store_text = encode_base16(wire_tx.to_data( ! wire));

    auto const data_store = to_chunk(tx3_store_serialized_v3);
    data_source store_stream(data_store);
    byte_reader reader2(data_store);
    result = chain::transaction::from_data(reader2, !wire);
    REQUIRE(result);
    auto const store_tx = std::move(*result);
    REQUIRE(data_store == store_tx.to_data( ! wire));
    REQUIRE(wire_tx == store_tx);
}

TEST_CASE("chain transaction  factory data 1  case 1  success", "[chain transaction]") {
    static auto const tx_hash = tx1_hash;
    static auto const raw_tx = to_chunk(tx1);
    REQUIRE(raw_tx.size() == 225u);

    byte_reader reader(raw_tx);
    auto result_exp = chain::transaction::from_data(reader, true);
    REQUIRE(result_exp);
    auto const tx = std::move(*result_exp);

    REQUIRE(tx.is_valid());
    REQUIRE(tx.serialized_size() == 225u);
    REQUIRE(tx.hash() == tx_hash);

    // Re-save tx and compare against original.
    REQUIRE(tx.serialized_size() == raw_tx.size());
    data_chunk resave = tx.to_data();
    REQUIRE(resave == raw_tx);
}

TEST_CASE("chain transaction  factory data 1  case 2  success", "[chain transaction]") {
    static auto const tx_hash = tx4_hash;
    static auto const raw_tx = to_chunk(tx4);
    REQUIRE(raw_tx.size() == 523u);

    byte_reader reader(raw_tx);
    auto result_exp = chain::transaction::from_data(reader, true);
    REQUIRE(result_exp);
    auto const tx = std::move(*result_exp);
    
    REQUIRE(tx.is_valid());
    REQUIRE(tx.hash() == tx_hash);

    // Re-save tx and compare against original.
    REQUIRE(tx.serialized_size() == raw_tx.size());
    data_chunk resave = tx.to_data();
    REQUIRE(resave == raw_tx);
}

TEST_CASE("chain transaction  version  roundtrip  success", "[chain transaction]") {
    uint32_t version = 1254u;
    chain::transaction instance;
    REQUIRE(version != instance.version());
    instance.set_version(version);
    REQUIRE(version == instance.version());
}

TEST_CASE("chain transaction  locktime  roundtrip  success", "[chain transaction]") {
    uint32_t locktime = 1254u;
    chain::transaction instance;
    REQUIRE(locktime != instance.locktime());
    instance.set_locktime(locktime);
    REQUIRE(locktime == instance.locktime());
}

TEST_CASE("chain transaction  inputs setter 1  roundtrip  success", "[chain transaction]") {
    data_chunk data = to_chunk(tx0_inputs);
    byte_reader reader(data);
    auto result_exp = chain::input::from_data(reader, true);
    REQUIRE(result_exp);
    chain::input::list inputs {
        std::move(*result_exp)
    };

    chain::transaction instance;
    REQUIRE(inputs != instance.inputs());
    instance.set_inputs(inputs);
    REQUIRE(inputs == instance.inputs());
}

TEST_CASE("chain transaction  inputs setter 2  roundtrip  success", "[chain transaction]") {
    data_chunk data = to_chunk(tx0_inputs);
    byte_reader reader(data);
    auto result_exp = chain::input::from_data(reader, true);
    REQUIRE(result_exp);
    chain::input::list inputs {
        std::move(*result_exp)
    };

    // This must be non-const.
    auto dup_inputs = inputs;

    chain::transaction instance;
    REQUIRE(inputs != instance.inputs());
    instance.set_inputs(std::move(dup_inputs));
    REQUIRE(inputs == instance.inputs());
}

TEST_CASE("chain transaction  outputs setter 1  roundtrip  success", "[chain transaction]") {
    data_chunk data = to_chunk(tx0_inputs_last_output);
    byte_reader reader(data);
    auto result_exp = chain::output::from_data(reader);
    REQUIRE(result_exp);
    auto const outputs = chain::output::list {
        std::move(*result_exp)
    };

    chain::transaction instance;
    REQUIRE(outputs != instance.outputs());
    instance.set_outputs(outputs);
    REQUIRE(outputs == instance.outputs());
}

TEST_CASE("chain transaction  outputs setter 2  roundtrip  success", "[chain transaction]") {
    data_chunk data = to_chunk(tx0_inputs_last_output);
    byte_reader reader(data);
    auto result_exp = chain::output::from_data(reader);
    REQUIRE(result_exp);
    auto const outputs = chain::output::list {
        std::move(*result_exp)
    };

    // This must be non-const.
    auto dup_outputs = outputs;

    chain::transaction instance;
    REQUIRE(outputs != instance.outputs());
    instance.set_outputs(std::move(dup_outputs));
    REQUIRE(outputs == instance.outputs());
}

TEST_CASE("chain transaction  is oversized coinbase  non coinbase tx  returns false", "[chain transaction]") {
    static auto const data = to_chunk(tx5);
    chain::transaction instance;
    byte_reader reader(data);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE( ! instance.is_coinbase());
    REQUIRE( ! instance.is_oversized_coinbase());
}

TEST_CASE("chain transaction  is oversized coinbase  script size below min  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    instance.inputs().back().previous_output().set_index(chain::point::null_index);
    instance.inputs().back().previous_output().set_hash(null_hash);
    REQUIRE(instance.is_coinbase());
    REQUIRE(instance.inputs().back().script().serialized_size(false) < min_coinbase_size);
    REQUIRE(instance.is_oversized_coinbase());
}

TEST_CASE("chain transaction  is oversized coinbase  script size above max  returns true", "[chain transaction]") {
    chain::transaction instance;
    auto& inputs = instance.inputs();
    inputs.emplace_back();
    inputs.back().previous_output().set_index(chain::point::null_index);
    inputs.back().previous_output().set_hash(null_hash);
    
    data_chunk oversized_script(max_coinbase_size + 10);
    byte_reader reader(oversized_script);
    auto result = chain::script::from_data(reader, false);
    REQUIRE(result);
    inputs.back().set_script(std::move(*result));

    REQUIRE(instance.is_coinbase());
    REQUIRE(inputs.back().script().serialized_size(false) > max_coinbase_size);
    REQUIRE(instance.is_oversized_coinbase());
}

TEST_CASE("chain transaction  is oversized coinbase  script size within bounds  returns false", "[chain transaction]") {
    chain::transaction instance;
    auto& inputs = instance.inputs();
    inputs.emplace_back();
    inputs.back().previous_output().set_index(chain::point::null_index);
    inputs.back().previous_output().set_hash(null_hash);

    data_chunk valid_script(50);
    byte_reader reader(valid_script);
    auto result = chain::script::from_data(reader, false);
    REQUIRE(result);
    inputs.back().set_script(std::move(*result));

    REQUIRE(instance.is_coinbase());
    REQUIRE(inputs.back().script().serialized_size(false) >= min_coinbase_size);
    REQUIRE(inputs.back().script().serialized_size(false) <= max_coinbase_size);
    REQUIRE( ! instance.is_oversized_coinbase());
}

TEST_CASE("chain transaction  is null non coinbase  coinbase tx  returns false", "[chain transaction]") {
    static auto const data = to_chunk(tx6);
    chain::transaction instance;
    byte_reader reader(data);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE( ! instance.is_null_non_coinbase());
}

TEST_CASE("chain transaction  is null non coinbase  no null input prevout  returns false", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_coinbase());
    REQUIRE( ! instance.is_null_non_coinbase());
}

TEST_CASE("chain transaction  is null non coinbase  null input prevout  returns true", "[chain transaction]") {
    chain::transaction instance;
    auto& inputs = instance.inputs();
    inputs.emplace_back();
    inputs.emplace_back();
    inputs.back().previous_output().set_index(chain::point::null_index);
    inputs.back().previous_output().set_hash(null_hash);
    REQUIRE( ! instance.is_coinbase());
    REQUIRE(instance.inputs().back().previous_output().is_null());
    REQUIRE(instance.is_null_non_coinbase());
}

TEST_CASE("chain transaction  total input value  no cache  returns zero", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    instance.inputs().emplace_back();
    REQUIRE(instance.total_input_value() == 0u);
}

TEST_CASE("chain transaction  total input value  cache  returns cache value sum", "[chain transaction]") {
    chain::transaction instance;
    auto& inputs = instance.inputs();
    inputs.emplace_back();
    inputs.back().previous_output().validation.cache.set_value(123u);
    inputs.emplace_back();
    inputs.back().previous_output().validation.cache.set_value(321u);
    REQUIRE(instance.total_input_value() == 444u);
}

TEST_CASE("chain transaction  total output value  empty outputs  returns zero", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE(instance.total_output_value() == 0u);
}

TEST_CASE("chain transaction  total output value  non empty outputs  returns sum", "[chain transaction]") {
    chain::transaction instance;

    // This must be non-const.
    chain::output::list outputs;

    outputs.emplace_back();
    outputs.back().set_value(1200);
    outputs.emplace_back();
    outputs.back().set_value(34);
    instance.set_outputs(std::move(outputs));
    REQUIRE(instance.total_output_value() == 1234u);
}

TEST_CASE("chain transaction  fees  nonempty  returns outputs minus inputs", "[chain transaction]") {
    chain::transaction instance;
    auto& inputs = instance.inputs();
    inputs.emplace_back();
    inputs.back().previous_output().validation.cache.set_value(123u);
    inputs.emplace_back();
    inputs.back().previous_output().validation.cache.set_value(321u);
    instance.outputs().emplace_back();
    instance.outputs().back().set_value(44u);
    REQUIRE(instance.fees() == 400u);
}

TEST_CASE("chain transaction  is overspent  output does not exceed input  returns false", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_overspent());
}

TEST_CASE("chain transaction  is overspent  output exceeds input  returns true", "[chain transaction]") {
    chain::transaction instance;
    auto& outputs = instance.outputs();
    outputs.emplace_back();
    outputs.back().set_value(1200);
    outputs.emplace_back();
    outputs.back().set_value(34);
    REQUIRE(instance.is_overspent());
}

// TODO(legacy): tests with initialized data
TEST_CASE("chain transaction  signature operations single input output uninitialized  returns zero", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    instance.outputs().emplace_back();
    REQUIRE(instance.signature_operations(false, false) == 0u);
}

TEST_CASE("chain transaction  is missing previous outputs  empty inputs  returns false", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_missing_previous_outputs());
}

TEST_CASE("chain transaction  is missing previous outputs  inputs without cache value  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    REQUIRE(instance.is_missing_previous_outputs());
}

TEST_CASE("chain transaction  is missing previous outputs  inputs with cache value  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    instance.inputs().back().previous_output().validation.cache.set_value(123u);
    REQUIRE( ! instance.is_missing_previous_outputs());
}

////TEST_CASE("chain transaction  missing previous outputs  empty inputs  returns empty", "[None]")
////{
////    chain::transaction instance;
////    REQUIRE(instance.missing_previous_outputs().size() == 0u);
////}
////
////TEST_CASE("chain transaction  missing previous outputs  inputs without cache value  returns single index", "[None]")
////{
////    chain::transaction instance;
////    instance.inputs().emplace_back();
////    auto result = instance.missing_previous_outputs();
////    REQUIRE(result.size() == 1u);
////    REQUIRE(result.back() == 0u);
////}
////
////TEST_CASE("chain transaction  missing previous outputs  inputs with cache value  returns empty", "[None]")
////{
////    chain::transaction instance;
////    instance.inputs().emplace_back();
////    instance.inputs().back().previous_output().validation.cache.set_value(123u);
////    REQUIRE(instance.missing_previous_outputs().size() == 0u);
////}

TEST_CASE("chain transaction  is double spend  empty inputs  returns false", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_double_spend(false));
    REQUIRE( ! instance.is_double_spend(true));
}

TEST_CASE("chain transaction  is double spend  unspent inputs  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    REQUIRE( ! instance.is_double_spend(false));
    REQUIRE( ! instance.is_double_spend(true));
}

TEST_CASE("chain transaction  is double spend  include unconfirmed false with unconfirmed  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    instance.inputs().back().previous_output().validation.spent = true;
    REQUIRE( ! instance.is_double_spend(false));
}

TEST_CASE("chain transaction  is double spend  include unconfirmed false with confirmed  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    instance.inputs().back().previous_output().validation.spent = true;
    instance.inputs().back().previous_output().validation.confirmed = true;
    REQUIRE(instance.is_double_spend(false));
}

TEST_CASE("chain transaction  is double spend  include unconfirmed true with unconfirmed  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back();
    instance.inputs().back().previous_output().validation.spent = true;
    REQUIRE(instance.is_double_spend(true));
}

TEST_CASE("chain transaction  is dusty  no outputs zero  returns false", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE( ! instance.is_dusty(0));
}

TEST_CASE("chain transaction  is dusty  two outputs limit above both  returns true", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx1);
    chain::transaction instance;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_dusty(1740950001));
}

TEST_CASE("chain transaction  is dusty  two outputs limit below both  returns false", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx1);
    chain::transaction instance;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE( ! instance.is_dusty(257999999));
}

TEST_CASE("chain transaction  is dusty  two outputs limit at upper  returns true", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx1);
    chain::transaction instance;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_dusty(1740950000));
}

TEST_CASE("chain transaction  is dusty  two outputs limit at lower  returns false", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx1);
    chain::transaction instance;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE( ! instance.is_dusty(258000000));
}

TEST_CASE("chain transaction  is dusty  two outputs limit between both  returns true", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx1);
    chain::transaction instance;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_dusty(258000001));
}

auto const hash1 = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash;

TEST_CASE("chain transaction  is mature  no inputs  returns true", "[chain transaction]") {
    chain::transaction instance;
    REQUIRE(instance.is_mature(453));
}

TEST_CASE("chain transaction  is mature  mature coinbase prevout  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back(chain::output_point{hash1, 42}, chain::script{}, 0);
    instance.inputs().back().previous_output().validation.coinbase = true;
    REQUIRE( ! instance.inputs().back().previous_output().is_null());
    REQUIRE(instance.is_mature(453));
}

TEST_CASE("chain transaction  is mature  premature coinbase prevout  returns false", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back(chain::output_point{hash1, 42}, chain::script{}, 0);
    instance.inputs().back().previous_output().validation.height = 20;
    instance.inputs().back().previous_output().validation.coinbase = true;
    REQUIRE( ! instance.inputs().back().previous_output().is_null());
    REQUIRE( ! instance.is_mature(50));
}

TEST_CASE("chain transaction  is mature  premature coinbase prevout null input  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back(chain::output_point{null_hash, chain::point::null_index}, chain::script{}, 0);
    instance.inputs().back().previous_output().validation.height = 20;
    instance.inputs().back().previous_output().validation.coinbase = true;
    REQUIRE(instance.inputs().back().previous_output().is_null());
    REQUIRE(instance.is_mature(50));
}

TEST_CASE("chain transaction  is mature  mature non coinbase prevout  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back(chain::output_point{hash1, 42}, chain::script{}, 0);
    instance.inputs().back().previous_output().validation.coinbase = false;
    REQUIRE( ! instance.inputs().back().previous_output().is_null());
    REQUIRE(instance.is_mature(453));
}

TEST_CASE("chain transaction  is mature  premature non coinbase prevout  returns true", "[chain transaction]") {
    chain::transaction instance;
    instance.inputs().emplace_back(chain::output_point{hash1, 42}, chain::script{}, 0);
    instance.inputs().back().previous_output().validation.height = 20;
    instance.inputs().back().previous_output().validation.coinbase = false;
    REQUIRE( ! instance.inputs().back().previous_output().is_null());
    REQUIRE(instance.is_mature(50));
}

TEST_CASE("chain transaction  operator assign equals 1  always  matches equivalent", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx4);
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    auto const expected = std::move(*result);
    reader.reset();
    auto result_exp = chain::transaction::from_data(reader, true);
    REQUIRE(result_exp);
    auto const instance = std::move(*result_exp);
    REQUIRE(instance == expected);
}

TEST_CASE("chain transaction  operator assign equals 2  always  matches equivalent", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx4);
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::transaction instance;
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("chain transaction  operator boolean equals  duplicates  returns true", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx4);
    chain::transaction alpha;
    chain::transaction beta;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("chain transaction  operator boolean equals  differs  returns false", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx4);
    chain::transaction alpha;
    chain::transaction beta;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE( ! (alpha == beta));
}

TEST_CASE("chain transaction  operator boolean not equals  duplicates  returns false", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx4);
    chain::transaction alpha;
    chain::transaction beta;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE( ! (alpha != beta));
}

TEST_CASE("chain transaction  operator boolean not equals  differs  returns true", "[chain transaction]") {
    static auto const raw_tx = to_chunk(tx4);
    chain::transaction alpha;
    chain::transaction beta;
    byte_reader reader(raw_tx);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("chain transaction  hash  block320670  success", "[chain transaction]") {
    // This is a garbage script that collides with the former opcode::raw_data sentinel.
    static auto const expected = tx7_hash;
    static auto const data = to_chunk(tx7);
    chain::transaction instance;
    byte_reader reader(data);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance.hash());
    REQUIRE(data == instance.to_data());
}

