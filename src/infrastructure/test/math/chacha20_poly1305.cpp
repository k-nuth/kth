// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <string>

#include <kth/infrastructure.hpp>
#include <kth/infrastructure/math/chacha20_poly1305.hpp>
#include <kth/infrastructure/math/hash.hpp>

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

// RFC 8439 appendix A, as printed there.
struct block_vector { char const* key; char const* nonce; uint32_t counter; char const* keystream; };
block_vector const appendix_a1[] = {
    {"0000000000000000000000000000000000000000000000000000000000000000",
     "000000000000000000000000", 0,
     "76b8e0ada0f13d90405d6ae55386bd28bdd219b8a08ded1aa836efcc8b770dc7da41597c5157488d7724e03fb8d84a37"
     "6a43b8f41518a11cc387b669b2ee6586"},
    {"0000000000000000000000000000000000000000000000000000000000000000",
     "000000000000000000000000", 1,
     "9f07e7be5551387a98ba977c732d080dcb0f29a048e3656912c6533e32ee7aed29b721769ce64e43d57133b074d839d5"
     "31ed1f28510afb45ace10a1f4b794d6f"},
    {"0000000000000000000000000000000000000000000000000000000000000001",
     "000000000000000000000000", 1,
     "3aeb5224ecf849929b9d828db1ced4dd832025e8018b8160b82284f3c949aa5a8eca00bbb4a73bdad192b5c42f73f2fd"
     "4e273644c8b36125a64addeb006c13a0"},
    {"00ff000000000000000000000000000000000000000000000000000000000000",
     "000000000000000000000000", 2,
     "72d54dfbf12ec44b362692df94137f328fea8da73990265ec1bbbea1ae9af0ca13b25aa26cb4a648cb9b9d1be65b2c09"
     "24a66c54d545ec1b7374f4872e99f096"},
    {"0000000000000000000000000000000000000000000000000000000000000000",
     "000000000000000000000002", 0,
     "c2c64d378cd536374ae204b9ef933fcd1a8b2288b3dfa49672ab765b54ee27c78a970e0e955c14f3a88e741b97c286f7"
     "5f8fc299e8148362fa198a39531bed6d"},
};

struct encryption_vector { char const* key; char const* nonce; uint32_t counter; char const* plaintext; char const* ciphertext; };
encryption_vector const appendix_a2[] = {
    {"0000000000000000000000000000000000000000000000000000000000000000",
     "000000000000000000000000", 0,
     "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
     "00000000000000000000000000000000",
     "76b8e0ada0f13d90405d6ae55386bd28bdd219b8a08ded1aa836efcc8b770dc7da41597c5157488d7724e03fb8d84a37"
     "6a43b8f41518a11cc387b669b2ee6586"},
    {"0000000000000000000000000000000000000000000000000000000000000001",
     "000000000000000000000002", 1,
     "416e79207375626d697373696f6e20746f20746865204945544620696e74656e6465642062792074686520436f6e7472"
     "696275746f7220666f72207075626c69636174696f6e20617320616c6c206f722070617274206f6620616e2049455446"
     "20496e7465726e65742d4472616674206f722052464320616e6420616e792073746174656d656e74206d616465207769"
     "7468696e2074686520636f6e74657874206f6620616e204945544620616374697669747920697320636f6e7369646572"
     "656420616e20224945544620436f6e747269627574696f6e222e20537563682073746174656d656e747320696e636c75"
     "6465206f72616c2073746174656d656e747320696e20494554462073657373696f6e732c2061732077656c6c20617320"
     "7772697474656e20616e6420656c656374726f6e696320636f6d6d756e69636174696f6e73206d61646520617420616e"
     "792074696d65206f7220706c6163652c207768696368206172652061646472657373656420746f",
     "a3fbf07df3fa2fde4f376ca23e82737041605d9f4f4f57bd8cff2c1d4b7955ec2a97948bd3722915c8f3d337f7d37005"
     "0e9e96d647b7c39f56e031ca5eb6250d4042e02785ececfa4b4bb5e8ead0440e20b6e8db09d881a7c6132f420e527950"
     "42bdfa7773d8a9051447b3291ce1411c680465552aa6c405b7764d5e87bea85ad00f8449ed8f72d0d662ab052691ca66"
     "424bc86d2df80ea41f43abf937d3259dc4b2d0dfb48a6c9139ddd7f76966e928e635553ba76c5c879d7b35d49eb2e62b"
     "0871cdac638939e25e8a1e0ef9d5280fa8ca328b351c3c765989cbcf3daa8b6ccc3aaf9f3979c92b3720fc88dc95ed84"
     "a1be059c6499b9fda236e7e818b04b0bc39c1e876b193bfe5569753f88128cc08aaa9b63d1a16f80ef2554d7189c411f"
     "5869ca52c5b83fa36ff216b9c1d30062bebcfd2dc5bce0911934fda79a86f6e698ced759c3ff9b6477338f3da4f9cd85"
     "14ea9982ccafb341b2384dd902f3d1ab7ac61dd29c6f21ba5b862f3730e37cfdc4fd806c22f221"},
    {"1c9240a5eb55d38af333888604f6b5f0473917c1402b80099dca5cbc207075c0",
     "000000000000000000000002", 42,
     "2754776173206272696c6c69672c20616e642074686520736c6974687920746f7665730a446964206779726520616e64"
     "2067696d626c6520696e2074686520776162653a0a416c6c206d696d737920776572652074686520626f726f676f7665"
     "732c0a416e6420746865206d6f6d65207261746873206f757467726162652e",
     "62e6347f95ed87a45ffae7426f27a1df5fb69110044c0d73118effa95b01e5cf166d3df2d721caf9b21e5fb14c616871"
     "fd84c54f9d65b283196c7fe4f60553ebf39c6402c42234e32a356b3e764312a61a5532055716ead6962568f87d3f3f77"
     "04c6a8d1bcd1bf4d50d6154b6da731b187b58dfd728afa36757a797ac188d1"},
};

struct mac_vector { char const* key; char const* message; char const* tag; };
mac_vector const appendix_a3[] = {
    {"0000000000000000000000000000000000000000000000000000000000000000",
     "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
     "00000000000000000000000000000000",
     "00000000000000000000000000000000"},
    {"0000000000000000000000000000000036e5f6b5c5e06070f0efca96227a863e",
     "416e79207375626d697373696f6e20746f20746865204945544620696e74656e6465642062792074686520436f6e7472"
     "696275746f7220666f72207075626c69636174696f6e20617320616c6c206f722070617274206f6620616e2049455446"
     "20496e7465726e65742d4472616674206f722052464320616e6420616e792073746174656d656e74206d616465207769"
     "7468696e2074686520636f6e74657874206f6620616e204945544620616374697669747920697320636f6e7369646572"
     "656420616e20224945544620436f6e747269627574696f6e222e20537563682073746174656d656e747320696e636c75"
     "6465206f72616c2073746174656d656e747320696e20494554462073657373696f6e732c2061732077656c6c20617320"
     "7772697474656e20616e6420656c656374726f6e696320636f6d6d756e69636174696f6e73206d61646520617420616e"
     "792074696d65206f7220706c6163652c207768696368206172652061646472657373656420746f",
     "36e5f6b5c5e06070f0efca96227a863e"},
    {"36e5f6b5c5e06070f0efca96227a863e00000000000000000000000000000000",
     "416e79207375626d697373696f6e20746f20746865204945544620696e74656e6465642062792074686520436f6e7472"
     "696275746f7220666f72207075626c69636174696f6e20617320616c6c206f722070617274206f6620616e2049455446"
     "20496e7465726e65742d4472616674206f722052464320616e6420616e792073746174656d656e74206d616465207769"
     "7468696e2074686520636f6e74657874206f6620616e204945544620616374697669747920697320636f6e7369646572"
     "656420616e20224945544620436f6e747269627574696f6e222e20537563682073746174656d656e747320696e636c75"
     "6465206f72616c2073746174656d656e747320696e20494554462073657373696f6e732c2061732077656c6c20617320"
     "7772697474656e20616e6420656c656374726f6e696320636f6d6d756e69636174696f6e73206d61646520617420616e"
     "792074696d65206f7220706c6163652c207768696368206172652061646472657373656420746f",
     "f3477e7cd95417af89a6b8794c310cf0"},
    {"1c9240a5eb55d38af333888604f6b5f0473917c1402b80099dca5cbc207075c0",
     "2754776173206272696c6c69672c20616e642074686520736c6974687920746f7665730a446964206779726520616e64"
     "2067696d626c6520696e2074686520776162653a0a416c6c206d696d737920776572652074686520626f726f676f7665"
     "732c0a416e6420746865206d6f6d65207261746873206f757467726162652e",
     "4541669a7eaaee61e708dc7cbcc5eb62"},
    {"0200000000000000000000000000000000000000000000000000000000000000",
     "ffffffffffffffffffffffffffffffff",
     "03000000000000000000000000000000"},
    {"02000000000000000000000000000000ffffffffffffffffffffffffffffffff",
     "02000000000000000000000000000000",
     "03000000000000000000000000000000"},
    {"0100000000000000000000000000000000000000000000000000000000000000",
     "fffffffffffffffffffffffffffffffff0ffffffffffffffffffffffffffffff11000000000000000000000000000000",
     "05000000000000000000000000000000"},
    {"0100000000000000000000000000000000000000000000000000000000000000",
     "fffffffffffffffffffffffffffffffffbfefefefefefefefefefefefefefefe01010101010101010101010101010101",
     "00000000000000000000000000000000"},
    {"0200000000000000000000000000000000000000000000000000000000000000",
     "fdffffffffffffffffffffffffffffff",
     "faffffffffffffffffffffffffffffff"},
    {"0100000000000000040000000000000000000000000000000000000000000000",
     "e33594d7505e43b900000000000000003394d7505e4379cd010000000000000000000000000000000000000000000000"
     "01000000000000000000000000000000",
     "14000000000000005500000000000000"},
    {"0100000000000000040000000000000000000000000000000000000000000000",
     "e33594d7505e43b900000000000000003394d7505e4379cd010000000000000000000000000000000000000000000000",
     "13000000000000000000000000000000"},
};

struct key_vector { char const* key; char const* nonce; char const* one_time_key; };
key_vector const appendix_a4[] = {
    {"0000000000000000000000000000000000000000000000000000000000000000",
     "000000000000000000000000",
     "76b8e0ada0f13d90405d6ae55386bd28bdd219b8a08ded1aa836efcc8b770dc7"},
    {"0000000000000000000000000000000000000000000000000000000000000001",
     "000000000000000000000002",
     "ecfa254f845f647473d3cb140da9e87606cb33066c447b87bc2666dde3fbb739"},
    {"1c9240a5eb55d38af333888604f6b5f0473917c1402b80099dca5cbc207075c0",
     "000000000000000000000002",
     "965e3bc6f9ec7ed9560808f4d229f94b137ff275ca9b3fcbdd59deaad23310ae"},
};

// A.5: the ciphertext, nonce, AAD and received tag; the plaintext it opens to.
char const* const appendix_a5_key = "1c9240a5eb55d38af333888604f6b5f0473917c1402b80099dca5cbc207075c0";
char const* const appendix_a5_nonce = "000000000102030405060708";
char const* const appendix_a5_aad = "f33388860000000000004e91";
char const* const appendix_a5_ciphertext =
    "64a0861575861af460f062c79be643bd5e805cfd345cf389f108670ac76c8cb24c6cfc18755d43eea09ee94e382d26b0"
    "bdb7b73c321b0100d4f03b7f355894cf332f830e710b97ce98c8a84abd0b948114ad176e008d33bd60f982b1ff37c855"
    "9797a06ef4f0ef61c186324e2b3506383606907b6a7c02b0f9f6157b53c867e4b9166c767b804d46a59b5216cde7a4e9"
    "9040c5a40433225ee282a1b0a06c523eaf4534d7f83fa1155b0047718cbc546a0d072b04b3564eea1b422273f548271a"
    "0bb2316053fa76991955ebd63159434ecebb4e466dae5a1073a6727627097a1049e617d91d361094fa68f0ff77987130"
    "305beaba2eda04df997b714d6c6f2c29a6ad5cb4022b02709b";
char const* const appendix_a5_tag = "eead9d67890cbb22392336fea1851f38";
char const* const appendix_a5_plaintext =
    "496e7465726e65742d4472616674732061726520647261667420646f63756d656e74732076616c696420666f72206120"
    "6d6178696d756d206f6620736978206d6f6e74687320616e64206d617920626520757064617465642c207265706c6163"
    "65642c206f72206f62736f6c65746564206279206f7468657220646f63756d656e747320617420616e792074696d652e"
    "20497420697320696e617070726f70726961746520746f2075736520496e7465726e65742d4472616674732061732072"
    "65666572656e6365206d6174657269616c206f7220746f2063697465207468656d206f74686572207468616e20617320"
    "2fe2809c776f726b20696e2070726f67726573732e2fe2809d";

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

TEST_CASE("chacha20 matches the block function vectors of RFC 8439 appendix A.1", "[chacha20poly1305]") {
    for (auto const& vector : appendix_a1) {
        CAPTURE(vector.key, vector.nonce, vector.counter);
        data_chunk const zeros(64);
        CHECK(encode_base16(chacha20_xor(fixed<32>(vector.key), vector.counter,
            fixed<12>(vector.nonce), zeros)) == vector.keystream);
    }
}

TEST_CASE("chacha20 matches the encryption vectors of RFC 8439 appendix A.2", "[chacha20poly1305]") {
    for (auto const& vector : appendix_a2) {
        CAPTURE(vector.key, vector.nonce, vector.counter);
        auto const key = fixed<32>(vector.key);
        auto const nonce = fixed<12>(vector.nonce);
        auto const plaintext = bytes(vector.plaintext);
        CHECK(encode_base16(chacha20_xor(key, vector.counter, nonce, plaintext)) == vector.ciphertext);
        CHECK(chacha20_xor(key, vector.counter, nonce, bytes(vector.ciphertext)) == plaintext);
    }
}

TEST_CASE("poly1305 matches the vectors of RFC 8439 appendix A.3", "[chacha20poly1305]") {
    // Among them a zero r, which leaves the tag s whatever the text, and the
    // carries and final reductions of vectors 5 to 11.
    for (auto const& vector : appendix_a3) {
        CAPTURE(vector.key, vector.tag);
        CHECK(encode_base16(poly1305(fixed<32>(vector.key), bytes(vector.message))) == vector.tag);
    }
}

TEST_CASE("chacha20 poly1305 one-time keys match RFC 8439 appendix A.4", "[chacha20poly1305]") {
    // The one-time key is the first 32 bytes of the key stream at counter 0.
    for (auto const& vector : appendix_a4) {
        CAPTURE(vector.key, vector.nonce);
        data_chunk const zeros(32);
        CHECK(encode_base16(chacha20_xor(fixed<32>(vector.key), 0, fixed<12>(vector.nonce), zeros))
            == vector.one_time_key);
    }
}

TEST_CASE("chacha20 poly1305 opens the message of RFC 8439 appendix A.5", "[chacha20poly1305]") {
    auto const key = fixed<32>(appendix_a5_key);
    auto const nonce = fixed<12>(appendix_a5_nonce);
    auto const aad = bytes(appendix_a5_aad);
    auto sealed = bytes(appendix_a5_ciphertext);
    auto const tag = bytes(appendix_a5_tag);
    sealed.insert(sealed.end(), tag.begin(), tag.end());
    auto const opened = chacha20_poly1305_decrypt(key, nonce, aad, sealed);
    REQUIRE(opened);
    CHECK(encode_base16(*opened) == appendix_a5_plaintext);
    CHECK(chacha20_poly1305_encrypt(key, nonce, aad, *opened) == sealed);
    sealed.back() ^= 0x01;
    CHECK_FALSE(chacha20_poly1305_decrypt(key, nonce, aad, sealed));
}

TEST_CASE("chacha20 poly1305 seals inputs near the largest SV2 frame", "[chacha20poly1305]") {
    // A Noise block of SV2 carries at most 65519 bytes and a frame payload up
    // to 65535 before it is split. The expected tags and the SHA-256 of each
    // whole output were computed with OpenSSL 3 (Python cryptography 46).
    struct vector {
        size_t size;
        char const* tag;
        char const* sha256;
    };
    vector const vectors[] = {
    {65518, "582d9b6e182838e030ed8af84c7fdac1",
     "e95ad85b9b73fb8846749e3bb8c94d8cbae276114afbad174bf3f61c23d4bd47"},
    {65519, "11d0bf44c088d94367895663a0618ae0",
     "b24aadadce8eb710ee113d1d64aafe23b96dc7787290d36af92c24b7ab740607"},
    {65520, "29f5810b7050271cc4716ebbadf01547",
     "dc543e9e44682c4e34896fc6a1b7a59c3fa1372d90238d3441b20848ceec7e3f"},
    {65535, "f63a2a877571c4a1cfff8d7e4e4e361e",
     "cf39e39c2520a1f10fc05ffe74d0f0c8af3b5a8431ae012449dc535061a2eceb"},
    {65536, "f89e884b631376a1abed520c7f9e39d7",
     "6615ef2b2fb42b11a1670c90ef023aa11aabbb436bafba805077951d4665f40c"}
    };
    chacha20_key key;
    for (size_t i = 0; i < key.size(); ++i) key[i] = uint8_t(i);
    chacha20_nonce nonce;
    for (size_t i = 0; i < nonce.size(); ++i) nonce[i] = uint8_t(i);
    data_chunk const aad{'k', 't', 'h'};
    for (auto const& v : vectors) {
        CAPTURE(v.size);
        data_chunk plaintext(v.size);
        for (size_t i = 0; i < plaintext.size(); ++i) plaintext[i] = uint8_t(i * 7 + 3);
        auto const sealed = chacha20_poly1305_encrypt(key, nonce, aad, plaintext);
        REQUIRE(sealed.size() == v.size + poly1305_tag_size);
        CHECK(encode_base16(byte_span(sealed).last(poly1305_tag_size)) == v.tag);
        CHECK(encode_base16(sha256_hash(sealed)) == v.sha256);
        auto const opened = chacha20_poly1305_decrypt(key, nonce, aad, sealed);
        REQUIRE(opened);
        CHECK(*opened == plaintext);
    }
}
