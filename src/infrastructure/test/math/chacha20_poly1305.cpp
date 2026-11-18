// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <string>

#include <kth/infrastructure.hpp>
#include <kth/infrastructure/math/chacha20_poly1305.hpp>
#include <kth/infrastructure/math/hkdf.hpp>

using namespace kth;

namespace {

data_chunk bytes(std::string const& text) {
    auto const out = decode_base16(text);
    REQUIRE(out);
    return *out;
}

template <size_t Size>
byte_array<Size> fixed(std::string const& text) {
    auto const out = decode_base16<Size>(text);
    REQUIRE(out);
    return *out;
}

struct aead_vector {
    char const* key;
    char const* nonce;
    char const* aad;
    char const* plaintext;
    char const* sealed;   // ciphertext followed by the tag
};

// RFC 8439 2.8.2, then lengths around the 16-byte padding and 64-byte block
// boundaries. The expected values beyond the RFC's were computed with OpenSSL
// 3.5 (through Node 24's crypto module) from the same inputs.
aead_vector const aead_vectors[] = {
    {"808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f",
     "070000004041424344454647",
     "50515253c0c1c2c3c4c5c6c7",
     "4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e",
     "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691"},
    {"354fd16e318820143d49d0d1c8f33038f39bd6e35b27719cf910a2f183a51b04",
     "d4cdc5998eacd3693404f0f6",
     "",
     "",
     "578f886258e84244a8c4cf222b3c4078"},
    {"6242598cd16178c3807f17502dc46106b570212ef069e688975f024b4a2395ce",
     "95d03093458b22bf43be10d1",
     "9548f68b7210d1009ab2091b",
     "",
     "82dd5ac90d0a2216d047abd9705294d1"},
    {"2fb52864c5af5b671bf8c571a7bd13d03efe14d225bb144037861b1ba97f551e",
     "17181d9d024dca707f3c1bba",
     "",
     "e3",
     "a08cf9f207eff0ea198940335b75968eb5"},
    {"0eb8108a97a34a9f6203171a6257daebdf96c6f1297d2cc8444e958da456b723",
     "b8a6e763529a1fd254f151ec",
     "b0805c73dc4857bc00885199",
     "73",
     "a42dd488214fd04aa4debc4e6ae0f5a439"},
    {"af55534d4361e629a4a802ea4e711141d0a06f437cab186e06d3f9132f4124eb",
     "6af8938b000e62323d58def0",
     "",
     "2a7ba709e1ed355abd682f1c425044",
     "1dcda8db842ef0d5567fdc3d3dc82cc1536105c87a34362711c6e022249258"},
    {"288fce14aa2f046a865b590538afb377fbb9f097d76dae3bf68142407b5b77f6",
     "b978d0c0fb477b8a64bf1a79",
     "fd5739dd30befc894ad25f0a",
     "2ae4534e93a48fdcac528a985514ef",
     "b769c4b7b721eaa649e8d5d804e3bc070a150fe65a101b0bd2fc3018ec7102"},
    {"7e50608ef61d2edeafe2f042b6c85311d2c313bba797c7aba4f250ae24b64735",
     "02be62d018adf3fa171bbbc3",
     "",
     "0e7f3469d45135701a9e728a79aee211",
     "866c3d97fcf0f428acb2cf4c72bee3350cda18889dfc5af860fbcae63b03327e"},
    {"176d2df3a4f01b2ca035e475767dc256e88cf03db1af475aec23ffe7c8baf6cd",
     "694aca30afb452c6eddbfe2b",
     "9aab3789b68337f85f2afab0",
     "b39e46f479c0322395e74f2395dec22b",
     "718f5180a0140a92fed21c3706938ffa99eeabf32c8d92da7c6b91de2a459aa0"},
    {"9091af632fd56217d98483de8180da593083e787d5e1d16f206596f64aee73a7",
     "f49cb7a26193efce1a10d40f",
     "",
     "0ead45b45b6f27c6cdd20c6bb1606c5100",
     "8b0b9e1740f55b05bacfbdc39d61615a26deb5d91d74763da9268e04456015137c"},
    {"d8a2b3f8d47728ce1525524d8e77f6fbdaf1384646465eb81d6acf2544c35463",
     "25173311feb12f9bcce65a36",
     "b685ad48090dabdad28378c3",
     "a95bc4d9ddb98e6be8cd9e34334662ada4",
     "65df36e31516b096678d259fa5f7ae425d6e3445e478a93b29685ddc1c33dd5a18"},
    {"2c5f8cb6e9953a72ef2ed64ee882b75cfd5d540830b47a1c92a9652f814f59ce",
     "f1a00762ca261834a06ec97c",
     "",
     "6e5f14fb3e370b0922cfbba7cc9145756119a0400e68b997079b7fa3d39dec52476af8b1892b43569d69aa60f4a20d18ebe4be2d126555ed7daf3c2ea49f7738",
     "807870651e5e9af46765c26b132adb04e457c12bad3fa23c12c6ae06ffd2fb39ec25501842fa50f99ea0208340f7a1034ab0231a63b18948df03e62037147e092365d1dfdfcacffc7606cc4f5e9781ff"},
    {"3eb50e6f49f6d7d3a173f7a4300465c9cb825ce91c3a103494366a100572a6d4",
     "e13d7f568ce69132881adbc8",
     "7051d0a83462fc0f49a4692e",
     "5926c558378fa496e64b773ad7c2e7b3e42f58d3fbef8b1d3145d3c0be9eb06d2cfe25f93ea20d6d9dfc8ce0ecc022ca04f0438df17b2a5defba04c3602ea18f",
     "9d9ddcc17d72d536977e7a446dfdf31d9f4df20093d354c287b1e805e0136001294fddd4e708f24f2ff9f2c1022fffd80adc5fcf912b4cbf20baa84ca2adc037a7675e1adf9aba418277e83ccdb61be7"},
    {"fea37cd3426d6e7f35ef921fd85bfe13222fa371dcd00ae8e3cb231ad443314a",
     "235d6b34999c481be277bdfd",
     "",
     "64a7acca93327d4ac7681cd2e80041c5e6186ec284b1d205851e7c26a413805d410f4d265e4f75b9afb91eecae0683760af3f3e5651b61ceb5fdb95d70b059764e",
     "8fe4f5060ce8c21932c5512c89e37b17e21b31413b8053a05411cf954ecc3f3987f62645621602b19b9369d7373ff0673b8c7737ae21e0d259b21ce242d6aadd82f7588608137913af6311f1a920528fed"},
    {"4559846aa0f8e837904625058146493fe919479149e3e3fccdea73732ed5fe00",
     "0154dbbc8a7a3bf9d5e084b4",
     "564f578b1a6ba5bbcaa6b8e3",
     "23cf35607b5f5fc8b028471ea501dbeeb8a6f29692b29e4a88fe6eef0c0fde2fe284e187bdc6081ae7c0aee11258959b471668ae7032bf58a58aeddc52d2252ce9",
     "1642bcd6621b2cfcc4c9cb4a31a69a43e698788b76db3902a1788d205ec8051faa16844f746b70456ba4df624a51714c2d99fe6f38b9e13cda79768c98bdcabf22f194b9e2f6d3bb0b8d54668c13fb70b0"},
};

} // namespace

TEST_CASE("chacha20 block function matches RFC 8439 2.3.2", "[chacha20poly1305]") {
    // The key stream block for counter 1 is what XOR with zeros returns.
    auto const key = fixed<32>("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    auto const nonce = fixed<12>("000000090000004a00000000");
    data_chunk const zeros(64);
    CHECK(encode_base16(chacha20_xor(key, 1, nonce, zeros)) ==
        "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e");
}

TEST_CASE("chacha20 key stream runs on across blocks and counters", "[chacha20poly1305]") {
    auto const key = fixed<32>("44c3ab57654cb35cbe4a8b3a50eb28f85b36598438e31dc433cbee152acae86c");
    auto const nonce = fixed<12>("37df31d293d75f4ac31d654b");
    data_chunk const zeros(130);
    // Computed with OpenSSL 3.5 (Node 24 crypto, "chacha20" with the counter
    // in the first four IV bytes).
    CHECK(encode_base16(chacha20_xor(key, 7, nonce, zeros)) ==
        "261df7fc054e405c26022f16b04b5d7d49cf7631cfc350448715230f056ce6488d9ded8463e9802c83bcc5492e74e473cf3cf7fed2c3becf992e086c2ae74a3031b753c71ae04f0610cb37af51c787ae0c01d3fdff1629c449ec9e39790e59aa77ba69046cf05abd9822c1035bd5b7f39c6bf1ca913dfabc8977da95b4d8da9c3e27");
    CHECK(chacha20_xor(key, 7, nonce, {}).empty());

    // XOR twice is the identity.
    auto const data = bytes("422f4248225969424f35306152b6e8fa658a4029c802e05cb92a7992e3dbf5772b162095d8aa0a002a15342b620539b8e5df1c392098eef05b50aa77376e1747415985ae0c3d2cc001a0ed8712d2cee4c4b842703055c9fae11785085e68b304985406836608d1a14d556c2f380259d6dadf4b1bea0d750e802a1070b9e33014561f");
    CHECK(chacha20_xor(key, 3, nonce, chacha20_xor(key, 3, nonce, data)) == data);
}

TEST_CASE("poly1305 matches RFC 8439 2.5.2 and the reference", "[chacha20poly1305]") {
    CHECK(encode_base16(poly1305(
        fixed<32>("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b"),
        bytes("43727970746f6772617068696320466f72756d2052657365617263682047726f7570"))) ==
        "a8061dc1305136c6c22b8baf0c0127a9");
    CHECK(encode_base16(poly1305(fixed<32>("0000d0bd20c7ba43c303172fc3252db65e9cf981e6c00cc57eb67f51f195e279"), bytes(""))) == "5e9cf981e6c00cc57eb67f51f195e279");
    CHECK(encode_base16(poly1305(fixed<32>("673233024f911f9903a5694fe81240c9b8a52bf4e810250ef3836d6a6a3ede89"), bytes("34"))) == "ae49c4995be01f08fe0788f59cfdf4aa");
    CHECK(encode_base16(poly1305(fixed<32>("519a02e389507e26a9f3a88b2b35f98042b520102981f571c0533ecb2665d5e9"), bytes("8c98fa1c339abf18098b7b9e0da0fa"))) == "90629b6d7540f4657ed2d02b4a045573");
    CHECK(encode_base16(poly1305(fixed<32>("44ff2e46f1d34e1fb3c6188761a0df8e5eac1541279f75f4c1442f4cc48633fc"), bytes("5ba60f94234478c20c011e8dcd924261"))) == "c9ef02da7052ae17a920097941d82acb");
    CHECK(encode_base16(poly1305(fixed<32>("84fbf09fdca0ea94e7717534716b3f911e2c011d496ca0f083d7b3c6751a73aa"), bytes("ada89c99cc4ca1a7d9e19c00ae8874a015"))) == "032ff15db952160f8c85d6986869eb46");
    CHECK(encode_base16(poly1305(fixed<32>("4d298017d15f377493af366d8367aaaf37a6b5024ca3acd2a4de0bfff4eee85f"), bytes("9077f961a063da4d76952802e6df06a46196be6fcd1571157733ed186a7fa98eda8fefe5d4a17b05ccd970c68070aa1b4a844699984b3b9c15abfcaecfac3c0eeb56ddac126b81aab3f2625ca5c7d095b02eb197417b303d5952c4015a4f7f7d82dbcd70"))) == "ea66b74ca2c0d871094d1490c7ca30fe");
    // All-ones key and message: clamping and the final reduction.
    CHECK(encode_base16(poly1305(fixed<32>("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), bytes("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"))) == "900fe32bc15fa8d7bca8efe4c7e37eb1");
}

TEST_CASE("chacha20 poly1305 seals and opens the reference vectors", "[chacha20poly1305]") {
    for (auto const& vector : aead_vectors) {
        CAPTURE(vector.plaintext, vector.aad);
        auto const key = fixed<32>(vector.key);
        auto const nonce = fixed<12>(vector.nonce);
        auto const aad = bytes(vector.aad);
        auto const plaintext = bytes(vector.plaintext);
        auto const sealed = chacha20_poly1305_encrypt(key, nonce, aad, plaintext);
        CHECK(encode_base16(sealed) == vector.sealed);
        auto const opened = chacha20_poly1305_decrypt(key, nonce, aad, sealed);
        REQUIRE(opened);
        CHECK(*opened == plaintext);
    }
}

TEST_CASE("chacha20 poly1305 refuses anything it did not seal", "[chacha20poly1305]") {
    auto const& vector = aead_vectors[0];
    auto const key = fixed<32>(vector.key);
    auto const nonce = fixed<12>(vector.nonce);
    auto const aad = bytes(vector.aad);
    auto const sealed = bytes(vector.sealed);
    REQUIRE(chacha20_poly1305_decrypt(key, nonce, aad, sealed));

    // A flipped bit in the ciphertext, then in the tag.
    for (size_t const position : {size_t{0}, sealed.size() - poly1305_tag_size - 1,
                                  sealed.size() - poly1305_tag_size, sealed.size() - 1}) {
        CAPTURE(position);
        auto changed = sealed;
        changed[position] ^= 0x01;
        CHECK_FALSE(chacha20_poly1305_decrypt(key, nonce, aad, changed));
    }

    // Different associated data, a different nonce, a different key.
    auto other_aad = aad;
    other_aad[0] ^= 0x80;
    CHECK_FALSE(chacha20_poly1305_decrypt(key, nonce, other_aad, sealed));
    CHECK_FALSE(chacha20_poly1305_decrypt(key, nonce, {}, sealed));
    auto other_nonce = nonce;
    other_nonce[11] ^= 0x01;
    CHECK_FALSE(chacha20_poly1305_decrypt(key, other_nonce, aad, sealed));
    auto other_key = key;
    other_key[0] ^= 0x01;
    CHECK_FALSE(chacha20_poly1305_decrypt(other_key, nonce, aad, sealed));

    // Too short to hold a tag, and a tag alone with nothing it covers.
    CHECK_FALSE(chacha20_poly1305_decrypt(key, nonce, aad, data_chunk(poly1305_tag_size - 1)));
    CHECK_FALSE(chacha20_poly1305_decrypt(key, nonce, aad, data_chunk(poly1305_tag_size)));
}

TEST_CASE("chacha20 poly1305 round-trips empty plaintext and associated data", "[chacha20poly1305]") {
    chacha20_key const key{};
    chacha20_nonce const nonce{};
    auto const sealed = chacha20_poly1305_encrypt(key, nonce, {}, {});
    CHECK(sealed.size() == poly1305_tag_size);
    auto const opened = chacha20_poly1305_decrypt(key, nonce, {}, sealed);
    REQUIRE(opened);
    CHECK(opened->empty());
}

TEST_CASE("hkdf sha256 matches RFC 5869 appendix A", "[hkdf]") {
    struct vector {
        char const* input_key;
        char const* salt;
        char const* info;
        size_t length;
        char const* output;
    };
    // Test cases 1 to 3 (SHA-256).
    vector const vectors[] = {
        {"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
         "000102030405060708090a0b0c",
         "f0f1f2f3f4f5f6f7f8f9",
         42,
         "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"},
        {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f",
         "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf",
         "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
         82,
         "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87"},
        {"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
         "",
         "",
         42,
         "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8"},
    };
    for (auto const& v : vectors) {
        CAPTURE(v.output);
        auto const prk = hkdf_sha256_extract(bytes(v.salt), bytes(v.input_key));
        auto const output = hkdf_sha256_expand(prk, bytes(v.info), v.length);
        REQUIRE(output);
        CHECK(encode_base16(*output) == v.output);
    }
}

TEST_CASE("hkdf sha256 refuses more than 255 blocks of output", "[hkdf]") {
    hash_digest const prk{};
    CHECK(hkdf_sha256_expand(prk, {}, 255 * 32));
    CHECK_FALSE(hkdf_sha256_expand(prk, {}, 255 * 32 + 1));
    auto const none = hkdf_sha256_expand(prk, {}, 0);
    REQUIRE(none);
    CHECK(none->empty());
}

TEST_CASE("hkdf sha256 pair is the first 64 bytes with empty info", "[hkdf]") {
    // Noise's HKDF(ck, ikm) for two outputs: HMAC(HMAC(ck, ikm), 0x01), then
    // HMAC(HMAC(ck, ikm), first || 0x02).
    auto const chaining_key = fixed<32>("56ddb6161ea73531ce31d9e4eb60b63c0900a889c6f8245ef6f185a443c5341a");
    auto const input = bytes("df9dc9469ff2c5ad549a7b71f7ade014c8a21cd431ee992e402ab2617b77f67e0f");
    auto const [first, second] = hkdf_sha256_pair(chaining_key, input);
    CHECK(encode_base16(first) + encode_base16(second) ==
        "e0ece8697a8fbfa7aa3576c8941132c7cadae2f80ff054043e13db373b0482d7d42605ae4c65f2aaf440a21e4c080bec95febf04100b14207291294b5dfdb661");
}
