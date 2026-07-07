// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/parser.hpp>

#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/config/checkpoint.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/impl/formats/base_16.ipp>

namespace kth::domain::config {

kth::infrastructure::config::checkpoint::list default_checkpoints(config::network network) {
    kth::infrastructure::config::checkpoint::list checkpoints;

#if defined(KTH_CURRENCY_BCH)
    if (network == domain::config::network::testnet) {
        checkpoints.reserve(42);
        checkpoints.emplace_back("000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"_hash, 0);
        checkpoints.emplace_back("00000000009e2958c15ff9290d571bf9459e93b19765c6801ddeccadbb160a1e"_hash, 100000);
        checkpoints.emplace_back("0000000000287bffd321963ef05feab753ebe274e1d78b2fd4e2bfe9ad3aa6f2"_hash, 200000);
        checkpoints.emplace_back("000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1"_hash, 300000);
        checkpoints.emplace_back("000000000598cbbb1e79057b79eef828c495d4fc31050e6b179c57d07d00367c"_hash, 400000);
        checkpoints.emplace_back("000000000001a7c0aaa2630fbb2c0e476aafffc60f82177375b2aaa22209f606"_hash, 500000);
        checkpoints.emplace_back("000000000000624f06c69d3a9fe8d25e0a9030569128d63ad1b704bbb3059a16"_hash, 600000);
        checkpoints.emplace_back("000000000000406178b12a4dea3b27e13b3c4fe4510994fd667d7c1e6a3f4dc1"_hash, 700000);
        checkpoints.emplace_back("0000000000209b091d6519187be7c2ee205293f25f9f503f90027e25abf8b503"_hash, 800000);
        checkpoints.emplace_back("0000000000356f8d8924556e765b7a94aaebc6b5c8685dcfa2b1ee8b41acd89b"_hash, 900000);
        checkpoints.emplace_back("0000000000478e259a3eda2fafbeeb0106626f946347955e99278fe6cc848414"_hash, 1000000);
        checkpoints.emplace_back("00000000001c2fb9880485b1f3d7b0ffa9fabdfd0cf16e29b122bb6275c73db0"_hash, 1100000);

        //2017-Aug Upgrade - Bitcoin Cash UAHF (1501590000)
        checkpoints.emplace_back("00000000ce6c653fea3cfcab6be13c232902271bc5f0bd0ac5047837cc4a2692"_hash, 1155874); //mediantime: 1501589615
        checkpoints.emplace_back("00000000f17c850672894b9a75b63a1e72830bbd5f4c8889b5c1a80e7faef138"_hash, 1155875); //mediantime: 1501590755. New rules activated in the next block.
        checkpoints.emplace_back("00000000000e38fef93ed9582a7df43815d5c2ba9fd37ef70c9a0ea4a285b8f5"_hash, 1155876); //mediantime: 1501591580. New rules activated in this block.

        //2017-Nov Upgrade - DAA - (1510600000)
        checkpoints.emplace_back("00000000001149a812d6ecb71aea7f298fd1b29aefb773fe380c1f3649c24b84"_hash, 1188696); //mediantime: 1510599261
        checkpoints.emplace_back("0000000000170ed0918077bde7b4d36cc4c91be69fa09211f748240dabe047fb"_hash, 1188697); //mediantime: 1510600611. New rules activated in the next block.
        checkpoints.emplace_back("0000000000051b450faa75bb8e1ea30bc18c4b9736e765d2794259a53bc83f99"_hash, 1188698); //mediantime: 1510602140. New rules activated in this block.

        checkpoints.emplace_back("00000000d91bdbb5394bcf457c0f0b7a7e43eb978e2d881b6c2a4c2756abc558"_hash, 1200000);

        //2018-May Upgrade - pythagoras - (1526400000)
        checkpoints.emplace_back("00000000000002830c09a7ec3411fb21f7b865063e385c7dc472a0c4ea279a8d"_hash, 1233068); //mediantime: 1526399556
        checkpoints.emplace_back("00000000001c8755e8b458194da11f061dbe294148b78b092354080b9e10fb81"_hash, 1233069); //mediantime: 1526400785. New rules activated in the next block.
        checkpoints.emplace_back("0000000000000253c6201a2076663cfe4722e4c75f537552cc4ce989d15f7cd5"_hash, 1233070); //mediantime: 1526400785. New rules activated in this block.
        checkpoints.emplace_back("00000000001e844212a0d7db21b5cca7cb3ceca7815a4a3f6c6a9e4de4d95049"_hash, 1233077); //mediantime: 1526406782. Old checkpoints
        checkpoints.emplace_back("0000000000327972b8470c11755adf8f4319796bafae01f5a6650490b98a17db"_hash, 1233078); //mediantime: 1526414036. Old checkpoints

        //2018-Nov Upgrade - euclid - (1542300000)
        checkpoints.emplace_back("000000000000014335e0f831a0703f57d3146d0913676ae26958c3a0ea46f210"_hash, 1267995); //mediantime: 1542298821
        checkpoints.emplace_back("00000000000001fae0095cd4bea16f1ce8ab63f3f660a03c6d8171485f484b24"_hash, 1267996); //mediantime: 1542300039. New rules activated in the next block.
        checkpoints.emplace_back("00000000000002773f8970352e4a3368a1ce6ef91eb606b64389b36fdbf1bd56"_hash, 1267997); //mediantime: 1542300144. New rules activated in this block.

        checkpoints.emplace_back("000000002a7a59c4f88a049fa5e405e67cd689d75a1f330cbf26286cf0ec1d8f"_hash, 1300000);

        //2019-May Upgrade - pisano - (1557921600)
        checkpoints.emplace_back("000000000000003f91fb869720bb76089ed6e93fbc5ae0e6f33309180cd71bbf"_hash, 1303883);  //mediantime: 1557921111
        checkpoints.emplace_back("00000000000001a749d7aa418c582a0e234ebc15643bf23a4f3107fa55120388"_hash, 1303884);  //mediantime: 1557922278. New rules activated in the next block.
        checkpoints.emplace_back("00000000000000479138892ef0e4fa478ccc938fb94df862ef5bde7e8dee23d3"_hash, 1303885);  //mediantime: 1557922620. New rules activated in this block.

        //2019-Nov Upgrade - mersenne - (1573819200)
        checkpoints.emplace_back("00000000283fcdb0c4be4939550f4f74ffc7c50c3305667770f7145ce7cf3c6d"_hash, 1341710);  //mediantime: 1573818600
        checkpoints.emplace_back("00000000c678f67ea16d5bf803f68ce42991839d13849f77332d6f586f62d421"_hash, 1341711);  //mediantime: 1573820238. New rules activated in the next block.
        checkpoints.emplace_back("00000000fffc44ea2e202bd905a9fbbb9491ef9e9d5a9eed4039079229afa35b"_hash, 1341712);  //mediantime: 1573821440. New rules activated in this block.

        //2020-May Upgrade - fermat - (1589544000)
        checkpoints.emplace_back("000000008eb40ed0c7567a0414a9da759ea93070187b1073e1b0b1099d4ee0fc"_hash, 1378459);  //mediantime: 1589543064
        checkpoints.emplace_back("0000000070f33c64cb94629680fbc57d17bea354a73e693affcb366d023db324"_hash, 1378460);  //mediantime: 1589544294. New rules activated in the next block.
        checkpoints.emplace_back("0000000099f5509b5f36b1926bcf82b21d936ebeadee811030dfbbb7fae915d7"_hash, 1378461);  //mediantime: 1589545500. New rules activated in this block.

        checkpoints.emplace_back("0000000000146da9eea6f299ca19ccb81371aa2e9490db229d610e74c4790e08"_hash, 1400000);

        //2020-Nov Upgrade - euler - (1605441600)
        checkpoints.emplace_back("000000000fc2ff8fc6585b71961c6ca0ecea24fa52746cb5484256019891e448"_hash, 1421480);  //mediantime: 1605441023
        checkpoints.emplace_back("00000000062c7f32591d883c99fc89ebe74a83287c0f2b7ffeef72e62217d40b"_hash, 1421481);  //mediantime: 1605442008. New rules activated in the next block.
        checkpoints.emplace_back("0000000023e0680a8a062b3cc289a4a341124ce7fcb6340ede207e194d73b60a"_hash, 1421482);  //mediantime: 1605443209. New rules activated in this block.

        // 2026-May Upgrade - leibniz - (1778846400)
        checkpoints.emplace_back("00000000078fe0c74ffa0c5221081c34774aa7d17a3c14c36f952f829463f120"_hash, 1710483);

    } else if (network == domain::config::network::testnet4) {
        checkpoints.reserve(21);
        checkpoints.emplace_back("000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b"_hash, 0);

        checkpoints.emplace_back("000000001d4ea1368ef790f94fd0f267814701d7ec0f5a5711d7dfe9b1f57ad6"_hash, 4);
        checkpoints.emplace_back("000000004d960d0917945707a8ea5b1addd708df728c9934129759a4dc690e07"_hash, 5);
        checkpoints.emplace_back("00000000d71b9b1f7e13b0c9b218a12df6526c1bcd1b667764b8693ae9a413cb"_hash, 6);

        checkpoints.emplace_back("000000000fb665479feed52f054dbcb37f9c9bdfef34a4bc18de6830177b954a"_hash, 2998);
        checkpoints.emplace_back("00000000147b82b15128419af04975959424db52be3e5a00b5eafd685fa8f316"_hash, 2999);
        checkpoints.emplace_back("00000000253cdbca769f44f8c54f49be05c1926bd856670c4cf387080d86ff3e"_hash, 3000);

        checkpoints.emplace_back("0000000095c97aff84da5b6df4cdb4f01846af0356426e87fdf53af0d4dcf258"_hash, 3998);
        checkpoints.emplace_back("0000000043fee6f7e3b8a55d7bef6798e4b1f0edc14b09f2f45e6cc849fe7e39"_hash, 3999);
        checkpoints.emplace_back("000000009e60279d380064a0faa63b70c3c9ecdad241186aa148641d84b005dd"_hash, 4000);

        checkpoints.emplace_back("000000007dcc1d2f26ea307552e1a34a76874669c8c7240b8eae022660f4fdbe"_hash, 4998);
        checkpoints.emplace_back("0000000060041222ce84c3f94161681e51b90c6db66f6a3cad0d01c07aecb2c1"_hash, 4999);
        checkpoints.emplace_back("000000009f092d074574a216faec682040a853c4f079c33dfd2c3ef1fd8108c4"_hash, 5000);

        checkpoints.emplace_back("0000000001579f88fcf71795ca12488987148b15d4204317bf02a413b2b9ef0a"_hash, 10000);
        checkpoints.emplace_back("00000000fc8eaff71ac9f2da72ce11f628491325784ba6bd7329add33ca8a2be"_hash, 15000);

        //2020-Nov Upgrade - euler - (1605441600)
        checkpoints.emplace_back("000000008bdc7862ad9368f78a952ec754f68db569b5e4620bf35d480d238155"_hash, 16843);  //mediantime: 1605436827
        checkpoints.emplace_back("00000000602570ee2b66c1d3f75d404c234f8aacdcc784da97e65838a2daf0fc"_hash, 16844);  //mediantime: 1605442049. New rules activated in the next block.
        checkpoints.emplace_back("00000000fb325b8f34fe80c96a5f708a08699a68bbab82dba4474d86bd743077"_hash, 16845);  //mediantime: 1605445733. New rules activated in this block.

        // 2026-May Upgrade - leibniz - (1778846400)
        checkpoints.emplace_back("00000000c45de7decceacab29002b82e454841dccf0a71db13b0572081d3cc8c"_hash, 305847);
        checkpoints.emplace_back("00000000601bf68f9927154cf55557b1c08245b4d415658d1cbcd1eb4502dabc"_hash, 305848);
        checkpoints.emplace_back("000000002da8249454eb69ea7f38fb455305a51d34224494ecd533ab36a489e6"_hash, 305849);
    } else if (network == domain::config::network::scalenet) {
        checkpoints.reserve(19);
        checkpoints.emplace_back("00000000e6453dc2dfe1ffa19023f86002eb11dbb8e87d0291a4599f0430be52"_hash, 0);
        checkpoints.emplace_back("0000000042d7fc947b3d2a5adcbc5ae787a287d266182b57e9e3911ba9ab818e"_hash, 1);

        checkpoints.emplace_back("000000005ab32d4f1406a2cbc2645b02c00372efb8b62902cea851f38c312fae"_hash, 4);
        checkpoints.emplace_back("000000006a7e3f4b23afec338d54ab10d10cf6b54a6c7c23f7a75dc63f57d8c4"_hash, 5);
        checkpoints.emplace_back("000000000e16730d293050fc5fe5b0978b858f5d9d91192a5ca2793902493597"_hash, 6);

        checkpoints.emplace_back("00000000057f804348338b9b5ab5f7d3b5ba26b9d747f5a7a2c2747c76f01043"_hash, 2998);
        checkpoints.emplace_back("000000002ae5c0057c63addba635e815e417633598c32d5593d4ddaed2c48e99"_hash, 2999);
        checkpoints.emplace_back("00000000184d3157b9d70c0ace0ac589460c13234f8f64deacb44827438338fb"_hash, 3000);

        checkpoints.emplace_back("000000007a82ae360e197df691e910452aa4ba763d68707f9e15d78ed4aeb4d8"_hash, 3998);
        checkpoints.emplace_back("000000006d25ccb206548c0dbd9f930aa9fc449e194efc914d99821429ce4c93"_hash, 3999);
        checkpoints.emplace_back("000000000135f1a55bdf08dacfc6c4e7508ff049434a9569eb17e92e3baa6bb3"_hash, 4000);

        checkpoints.emplace_back("000000007e2e84ce54bafad80ea19e9ed9c94d5b141013627e20b2cd81993138"_hash, 4998);
        checkpoints.emplace_back("000000005c7c5e3ff1d2b39e9c64cc200ce3e0673ff63a7b3dc3d6d55481088d"_hash, 4999);
        checkpoints.emplace_back("000000000a9cdd4e68092626528bb0afc32c1b87aa5f8fbfafe45e87d0016a2b"_hash, 5000);

        checkpoints.emplace_back("00000000b711dc753130e5083888d106f99b920b1b8a492eb5ac41d40e482905"_hash, 10000);
        checkpoints.emplace_back("00000000ec24d110081e8a6ab4b23b0ab716f419eb7428d95307028a4df975f5"_hash, 15000);

        //2020-Nov Upgrade - euler - (1605441600)
        checkpoints.emplace_back("000000000abfdc199d8afad460d834a68a6ef35bce1b8877b96e418530d47fc1"_hash, 16867);  //mediantime: 1605441188
        checkpoints.emplace_back("000000008b6a607a3a731ae1df816bb828450bec67fea5e8dbcf837ed711b99a"_hash, 16868);  //mediantime: 1605443014. New rules activated in the next block.
        checkpoints.emplace_back("00000000e4627a1a0bf9aaae007af5cea32720fb54cf2ccf0aa20b02a18392ab"_hash, 16869);  //mediantime: 1605444236. New rules activated in this block.

    } else if (network == domain::config::network::chipnet) {
        checkpoints.reserve(20);

        checkpoints.emplace_back("000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b"_hash, 0);


        checkpoints.emplace_back("000000009f092d074574a216faec682040a853c4f079c33dfd2c3ef1fd8108c4"_hash, 5000);

        // Axion activation.
        checkpoints.emplace_back("00000000fb325b8f34fe80c96a5f708a08699a68bbab82dba4474d86bd743077"_hash, 16845);
        checkpoints.emplace_back("000000000015197537e59f339e3b1bbf81a66f691bd3d7aa08560fc7bf5113fb"_hash, 38000);

        // Upgrade 7 ("tachyon") era (actual activation block was in the past significantly before this)
        checkpoints.emplace_back("00000000009af4379d87f17d0f172ee4769b48839a5a3a3e81d69da4322518b8"_hash, 54700);
        checkpoints.emplace_back("0000000000a2c2fc11a3b72adbd10a3f02a1f8745da55a85321523043639829a"_hash, 68117);

        // Upgrade 8; May 15, 2022 (MTP time >= 1652616000), first upgrade block: 95465
        checkpoints.emplace_back("00000000a77206a2265cabc47cc2c34706ba1c5e5a5743ac6681b83d43c91a01"_hash, 95465);

        // Fork block for chipnet
        checkpoints.emplace_back("00000000040ba9641ba98a37b2e5ceead38e4e2930ac8f145c8094f94c708727"_hash, 115252);
        checkpoints.emplace_back("000000006ad16ee5ee579bc3712b6f15cdf0a7f25a694e1979616794b73c5122"_hash, 115510);

        // Upgrade9 - first block mined under upgrade9 rules for chipnet (Nov. 15, 2022)
        checkpoints.emplace_back("0000000056087dee73fb66178ca70da89dfd0be098b1a63cf6fe93934cd04c78"_hash, 121957);
        checkpoints.emplace_back("000000000363cd56e49a46684cec1d99854c4aae662a6faee0df4c9a49dc8a33"_hash, 122396);
        checkpoints.emplace_back("0000000010e506eeb528dd8238947c6fcdf8d752ece66517eea778650600edae"_hash, 128042);
        checkpoints.emplace_back("000000009788ecce39b046caab3cf0f72e8c5409df23454679dbdcae2bd4dded"_hash, 148000);

        // A block significantly after Upgrade 10 activated (which activated on Nov. 15, 2023)
        checkpoints.emplace_back("000000003c37cc0372a5b9ccacca921786bbfc699722fc41e9fdbb1de4146ef1"_hash, 178140);
        checkpoints.emplace_back("00000000146a073b9d4e172adbee5252014a8b4d75c56cce36858311565ae251"_hash, 206364);

        // A block after Upgrade 11 activated (Nov. 15, 2024), first block after upgrade: 227229
        checkpoints.emplace_back("00000000144b00db5736b33bd572b3a3a52aa9b4c26ba59fc212aeb68a9b7a20"_hash, 228000);
        checkpoints.emplace_back("0000000017d92f88ed2c81885c57f999184860a042250510be06b3edd12e0dc5"_hash, 232000);

        // 2026-May Upgrade - leibniz - (1778846400)
        checkpoints.emplace_back("00000000261a64ba18da8edee2562957bad84c4d2405e5e4f044c5306fd3e195"_hash, 279791);
        checkpoints.emplace_back("0000000019fdedee4233df4236800ec8bbe8ce45185ff531f43b307db12122b4"_hash, 279792);
        checkpoints.emplace_back("00000000292db830fbd551b9b6ccf2a0e562acb6daf5b45d56588ec742d0ef1d"_hash, 279793);

    } else if (network == domain::config::network::mainnet) {
        checkpoints.reserve(92);
        checkpoints.emplace_back("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash,       0);
        checkpoints.emplace_back("0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d"_hash,  11'111);
        checkpoints.emplace_back("000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6"_hash,  33'333);
        checkpoints.emplace_back("00000000001e1b4903550a0b96e9a9405c8a95f387162e4944e8d9fbe501cd6a"_hash,  68'555);
        checkpoints.emplace_back("00000000006a49b14bcf27462068f1264c961f11fa2e0eddd2be0791e1d4124a"_hash,  70'567);
        checkpoints.emplace_back("0000000000573993a3c9e41ce34471c079dcf5f52a0e824a81e7f953b8661a20"_hash,  74'000);
        checkpoints.emplace_back("00000000000291ce28027faea320c8d2b054b2e0fe44a773f3eefb151d6bdc97"_hash, 105'000);
        checkpoints.emplace_back("000000000000774a7f8a7a12dc906ddb9e17e75d684f15e00f8767f9e8f36553"_hash, 118'000);
        checkpoints.emplace_back("00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe"_hash, 134'444);
        checkpoints.emplace_back("000000000000033b512028abb90e1626d8b346fd0ed598ac0a3c371138dce2bd"_hash, 140'700);

        checkpoints.emplace_back("000000000000099e61ea72015e79632f216fe6cb33d7899acb35b75c8303b763"_hash, 168'000);
        checkpoints.emplace_back("000000000000059f452a5f7340de6682a977387c17010ff6e6c3bd83ca8b1317"_hash, 193'000);
        checkpoints.emplace_back("000000000000048b95347e83192f69cf0366076336c639f9b7228e9ba171342e"_hash, 210'000);
        checkpoints.emplace_back("00000000000001b4f4b433e81ee46494af945cf96014816a4e2370f11b23df4e"_hash, 216'116);
        checkpoints.emplace_back("00000000000001c108384350f74090433e7fcf79a606b8e797f065b130575932"_hash, 225'430);
        checkpoints.emplace_back("000000000000003887df1f29024b06fc2200b55f8af8f35453d7be294df2d214"_hash, 250'000);
        checkpoints.emplace_back("0000000000000001ae8c72a0b0c301f67e3afca10e819efa9041e458e9bd7e40"_hash, 279'000);
        checkpoints.emplace_back("00000000000000004d9b4ef50f0f9d686fd69db2e03af35a100370c64632a983"_hash, 295'000);
        checkpoints.emplace_back("000000000000000082ccf8f1557c5d40b21edabb18d2d691cfbf87118bac7254"_hash, 300'000);
        checkpoints.emplace_back("00000000000000000409695bce21828b31a7143fa35fcab64670dd337a71425d"_hash, 325'000);

        checkpoints.emplace_back("0000000000000000053cf64f0400bb38e0c4b3872c38795ddde27acb40a112bb"_hash, 350'000);
        checkpoints.emplace_back("000000000000000009733ff8f11fbb9575af7412df3fae97f382376709c965dc"_hash, 375'000);
        checkpoints.emplace_back("000000000000000004ec466ce4732fe6f1ed1cddc2ed4b328fff5224276e3f6f"_hash, 400'000);
        checkpoints.emplace_back("00000000000000000142adfebcb9a0aa75f0c4980dd5c7dd17062bf7de77c16d"_hash, 425'000);
        checkpoints.emplace_back("0000000000000000014083723ed311a461c648068af8cef8a19dcd620c07a20b"_hash, 450'000);
        checkpoints.emplace_back("0000000000000000017c42fd88e78ab02c5f5c684f8344e1f5c9e4cebecde71c"_hash, 475'000);

        //2017-Aug Upgrade - Bitcoin Cash UAHF (1501590000)
        checkpoints.emplace_back("000000000000000000eb9bc1f9557dc9e2cfe576f57a52f6be94720b338029e4"_hash, 478'557);  //mediantime: 1501589769
        checkpoints.emplace_back("0000000000000000011865af4122fe3b144e2cbeea86142e8ff2fb4107352d43"_hash, 478'558);  //mediantime: 1501591048. New rules activated in the next block.
        checkpoints.emplace_back("000000000000000000651ef99cb9fcbe0dadde1d424bd9f15ff20136191a5eec"_hash, 478'559);  //mediantime: 1501592847. New rules activated in this block.

        checkpoints.emplace_back("000000000000000005e14d3f9fdfb70745308706615cfa9edca4f4558332b201"_hash, 500'000);

        //2017-Nov Upgrade - DAA - (1510600000)
        checkpoints.emplace_back("0000000000000000008088d63f48da98b7352ad7c4c85f3d90b657cf50ff1ede"_hash, 504'030);  //mediantime: 1510594229
        checkpoints.emplace_back("0000000000000000011ebf65b60d0a3de80b8175be709d653b4c1a1beeb6ab9c"_hash, 504'031);  //mediantime: 1510601033. New rules activated in the next block.
        checkpoints.emplace_back("00000000000000000343e9875012f2062554c8752929892c82a0c0743ac7dcfd"_hash, 504'032);  //mediantime: 1510601742. New rules activated in this block.

        checkpoints.emplace_back("0000000000000000001b09302aa6a8dc65b7542dd195866907dd4e4ccba30d58"_hash, 515'000);

        //2018-May Upgrade - pythagoras - (1526400000)
        checkpoints.emplace_back("0000000000000000018d22e0ca9c5f591fefd896abc905550745ed3190f749c3"_hash, 530'354);  //mediantime: 1526399926
        checkpoints.emplace_back("000000000000000000434c281fb3ed692efea5af769aedb090b2b9f395b5386e"_hash, 530'355);  //mediantime: 1526400858. New rules activated in the next block.
        checkpoints.emplace_back("0000000000000000013d91e08ec61cc99761751ef09c561209593eea6bb543da"_hash, 530'356);  //mediantime: 1526402161. New rules activated in this block.
        checkpoints.emplace_back("000000000000000000f59580044b235c4f46381cacd319d29e8bb21c517a771d"_hash, 530'357);  //mediantime: 1526402650. I put the following checkpoints for a historical inaccuracy in the Bitcoin ABC code: https://github.com/bitcoin-cash-node/bitcoin-cash-node/commit/97c32f461a1a6d6ca71c5958d67047a1c06d83fd#diff-ff53e63501a5e89fd650b378c9708274df8ad5d38fcffa6c64be417c4d438b6d
        checkpoints.emplace_back("00000000000000000031687b7320832e5035abe8e3f81fb71517fc541765de83"_hash, 530'358);  //mediantime: 1526405239
        checkpoints.emplace_back("0000000000000000011ada8bd08f46074f44a8f155396f43e38acf9501c49103"_hash, 530'359);  //mediantime: 1526410186
        checkpoints.emplace_back("00000000000000000195edc6e094ae1db6a274f1188127390a74a727db9a2717"_hash, 530'360);  //mediantime: 1526410326

        checkpoints.emplace_back("000000000000000000fc66aae55a178fec2ba2f2bc86eb6c6f632b5bc2b40af1"_hash, 545'000);

        //2018-Nov Upgrade - euclid - (1542300000)
        checkpoints.emplace_back("0000000000000000018b0da51421703b239218d0d99a6bf86a1fcdf266e3a5b1"_hash, 556'765);  //mediantime: 1542299730
        checkpoints.emplace_back("00000000000000000102d94fde9bd0807a2cc7582fe85dd6349b73ce4e8d9322"_hash, 556'766);  //mediantime: 1542300873. New rules activated in the next block.
        checkpoints.emplace_back("0000000000000000004626ff6e3b936941d341c5932ece4357eeccac44e6d56c"_hash, 556'767);  //mediantime: 1542301036. New rules activated in this block.

        checkpoints.emplace_back("0000000000000000039f4e03a756eaa5deb89ef9fa0d565a25473d5deb5e7b0d"_hash, 560'000);
        checkpoints.emplace_back("000000000000000003428b04e49a9a303afbedefe2ac9094d44dd127d7366a97"_hash, 575'000);

        //2019-May Upgrade - pisano - (1557921600)
        checkpoints.emplace_back("000000000000000002d39295b2433668b97720822a77d278bc2c66eb13891d62"_hash, 582'678);  //mediantime: 1557921209
        checkpoints.emplace_back("0000000000000000018596bdfd350a9fbc7297a62a3f510b74565d992d63d2ef"_hash, 582'679);  //mediantime: 1557921789. New rules activated in the next block.
        checkpoints.emplace_back("000000000000000001b4b8e36aec7d4f9671a47872cb9a74dc16ca398c7dcc18"_hash, 582'680);  //mediantime: 1557921810. New rules activated in this block.

        checkpoints.emplace_back("000000000000000001eb9b2786e6200beb37a20a4959a86c2b52adca2b23597b"_hash, 590'000);
        checkpoints.emplace_back("00000000000000000041048ecef77d6b9ccb4012c0c1012e72b6737220d3f910"_hash, 605'000);

        //2019-Nov Upgrade - mersenne - (1573819200)
        checkpoints.emplace_back("000000000000000000d03bd9b9a7d3b734b3910d8ae59d8ed237174517e8aaf9"_hash, 609'134);  //mediantime: 1573818748
        checkpoints.emplace_back("0000000000000000026f7ec9e79be2f5bb839f29ebcf734066d4bb9a13f6ea83"_hash, 609'135);  //mediantime: 1573819391. New rules activated in the next block.
        checkpoints.emplace_back("000000000000000000b48bb207faac5ac655c313e41ac909322eaa694f5bc5b1"_hash, 609'136);  //mediantime: 1573820367. New rules activated in this block.

        checkpoints.emplace_back("00000000000000000177593a9861113e263bd9fb7679d6783ab88829b147662a"_hash, 620'000);
        checkpoints.emplace_back("000000000000000001c885feaa06e225ee51c37c98a293ab779e01912a99a620"_hash, 635'000);

        //2020-May Upgrade - fermat - (1589544000)
        checkpoints.emplace_back("000000000000000002075bc3ceffc9277e74e03f71733eba006cc7a5adc27622"_hash, 635'257);  //mediantime: 1589543995
        checkpoints.emplace_back("000000000000000003302c47d01e78f1c86aa3b0e96b066761a5059bc8f5781a"_hash, 635'258);  //mediantime: 1589544126. New rules activated in the next block.
        checkpoints.emplace_back("00000000000000000033dfef1fc2d6a5d5520b078c55193a9bf498c5b27530f7"_hash, 635'259);  //mediantime: 1589544127. New rules activated in this block.

        checkpoints.emplace_back("000000000000000001e5a8e11a9a523e15ad985b8123df0f7b364ad8f83d82b0"_hash, 650'000);

        //2020-Nov Upgrade - euler - (1605441600)
        checkpoints.emplace_back("000000000000000003e4c72c4c4888a81b4f9c66a65cc98dbdbb2e56e0a2f72d"_hash, 661'646);  //mediantime: 1605439958
        checkpoints.emplace_back("00000000000000000083ed4b7a780d59e3983513215518ad75654bb02deee62f"_hash, 661'647);  //mediantime: 1605443067. New rules activated in the next block.
        checkpoints.emplace_back("0000000000000000029e471c41818d24b8b74c911071c4ef0b4a0509f9b5a8ce"_hash, 661'648);  //mediantime: 1605443708. New rules activated in this block.

        // There were no consensus changes in the May 2021.
        // There were no consensus changes in the November 2021.

        checkpoints.emplace_back("000000000000000002bf5f3f1f385c767ac78ab2db48abeaffff9d609b1b34ff"_hash, 665'000);
        checkpoints.emplace_back("00000000000000000040b37f904a9cbba25a6d37aa313d4ae8c4c46589cf4c6e"_hash, 680'000);
        checkpoints.emplace_back("000000000000000002796d49edb3fc3643d82808aa0babf55cb7deed8147446b"_hash, 695'000);
        checkpoints.emplace_back("00000000000000000259ad550b5420e5418cdfc14873d6985bcf1dfa261dbc9c"_hash, 710'000);
        checkpoints.emplace_back("00000000000000000545f96d55f3664d794c9940ae5b97dd66d9c6829c05bf23"_hash, 725'000);
        checkpoints.emplace_back("0000000000000000021c8878d6905b85ef4d4cf8ea0e036874ffeea13654561f"_hash, 740'000);

        //2022-May Upgrade - gauss - (1652616000)
        checkpoints.emplace_back("000000000000000000b92c50d56fc2c60b0502fffec6dc5410065a9b1e29109f"_hash, 740'236);
        checkpoints.emplace_back("0000000000000000018e572c8e2615b86c1c45c61be8a5f380a339abdff15caa"_hash, 740'237);
        checkpoints.emplace_back("000000000000000002afc6fbd302f01f8cf4533f4b45207abc61d9f4297bf969"_hash, 740'238);

        //2023-May Upgrade - descartes - (1684152000)
        checkpoints.emplace_back("000000000000000002b678c471841c3e404ec7ae9ca9c32026fe27eb6e3a1ed1"_hash, 792'772);
        checkpoints.emplace_back("000000000000000002fc0cdadaef1857bbd2936d37ea94f80ba3db4a5e8353e8"_hash, 792'773);
        checkpoints.emplace_back("00000000000000000340a607ca5e9a8b56b620297216edb818eb09e3c6e95609"_hash, 792'774);

        //2024-May Upgrade - lobachevski - (1715774400)
        checkpoints.emplace_back("000000000000000001806bcdde19c47f088a8026e24905ac6f75afe3ef83594c"_hash, 845'890);
        checkpoints.emplace_back("0000000000000000017012058e7b67032926f1f20f96d1a2cd66abff9aaf8244"_hash, 845'891);
        checkpoints.emplace_back("0000000000000000016e3353d5da435ef5d374abe90d9bb430c0408e423632d5"_hash, 845'892);

        checkpoints.emplace_back("00000000000000000071fe9c24aa2fe98d64120407b757a093bbb4a597781753"_hash, 860'000);
        checkpoints.emplace_back("00000000000000000100ada0fb390ef3fa8fe721fa12617313b8b32494ad7a2c"_hash, 875'000);
        checkpoints.emplace_back("000000000000000000bb3b5f03e5b04172a0b5a53f8661eb39d1c91b637ff54a"_hash, 890'000);

        // 2025-May Upgrade - galois - (1747310400)
        checkpoints.emplace_back("0000000000000000013cd2abfe36fa63976d293235e42ec08804143787b0a9db"_hash, 898'373);
        checkpoints.emplace_back("00000000000000000157a0a3dcdc80f1acd809648d238c1e893b26247091b3b4"_hash, 898'374);
        checkpoints.emplace_back("0000000000000000007e2e7dd49323c90d16fd76a521804d709f0d5a442fd42a"_hash, 898'375);

        checkpoints.emplace_back("000000000000000001fb491cce2c6d44f628e812519a2bc420bbc774cdc2d647"_hash, 905'000);
        checkpoints.emplace_back("0000000000000000016a9d52a9c0a7d7fd3cf1a7cea538565674613a65341186"_hash, 920'000);
        checkpoints.emplace_back("00000000000000000099a0c2d63cfa812992761580d7a8995fe699ee6a9c848e"_hash, 935'000);
        checkpoints.emplace_back("00000000000000000046655be4cd21fd8ee3c59cf9218f3b349c5ea78538fabf"_hash, 950'000);

        // 2026-May Upgrade - leibniz - (1778846400)
        checkpoints.emplace_back("0000000000000000009647b105551f1776362ef92f08aa0312311ff0e9fdad3f"_hash, 951'144);
        checkpoints.emplace_back("000000000000000000f64dbed68370486f945097ba90b17e30d7bf5c43df7a60"_hash, 951'145);
        checkpoints.emplace_back("0000000000000000005cc515e90a5f37ea3029d1e2741d8a00e863edb55f3d1d"_hash, 951'146);

        // //2027-May Upgrade - cantor - (1810382400)
        // checkpoints.emplace_back("", 0);
        // checkpoints.emplace_back("", 0);
        // checkpoints.emplace_back("", 0);
    } else {
        // BCH Regtest
        checkpoints.reserve(1);
        checkpoints.emplace_back("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"_hash, 0);
    }
#elif defined(KTH_CURRENCY_BTC)
    if (network == domain::config::network::testnet) {
        checkpoints.reserve(19);
        checkpoints.emplace_back("000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"_hash, 0);
        checkpoints.emplace_back("00000000009e2958c15ff9290d571bf9459e93b19765c6801ddeccadbb160a1e"_hash, 100000);
        checkpoints.emplace_back("0000000000287bffd321963ef05feab753ebe274e1d78b2fd4e2bfe9ad3aa6f2"_hash, 200000);
        checkpoints.emplace_back("000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1"_hash, 300000);
        checkpoints.emplace_back("000000000598cbbb1e79057b79eef828c495d4fc31050e6b179c57d07d00367c"_hash, 400000);
        checkpoints.emplace_back("000000000001a7c0aaa2630fbb2c0e476aafffc60f82177375b2aaa22209f606"_hash, 500000);
        checkpoints.emplace_back("000000000000624f06c69d3a9fe8d25e0a9030569128d63ad1b704bbb3059a16"_hash, 600000);
        checkpoints.emplace_back("000000000000406178b12a4dea3b27e13b3c4fe4510994fd667d7c1e6a3f4dc1"_hash, 700000);
        checkpoints.emplace_back("0000000000209b091d6519187be7c2ee205293f25f9f503f90027e25abf8b503"_hash, 800000);
        checkpoints.emplace_back("0000000000356f8d8924556e765b7a94aaebc6b5c8685dcfa2b1ee8b41acd89b"_hash, 900000);
        checkpoints.emplace_back("0000000000478e259a3eda2fafbeeb0106626f946347955e99278fe6cc848414"_hash, 1000000);
        checkpoints.emplace_back("00000000001c2fb9880485b1f3d7b0ffa9fabdfd0cf16e29b122bb6275c73db0"_hash, 1100000);

        //2017-Aug Upgrade - Bitcoin Cash UAHF (1501590000)
        checkpoints.emplace_back("00000000ce6c653fea3cfcab6be13c232902271bc5f0bd0ac5047837cc4a2692"_hash, 1155874); //mediantime: 1501589615
        checkpoints.emplace_back("00000000f17c850672894b9a75b63a1e72830bbd5f4c8889b5c1a80e7faef138"_hash, 1155875); //mediantime: 1501590755. New rules activated in the next block.
        checkpoints.emplace_back("0000000093b3cdf2b50a05fa1527810f52d6826781916ef129098e06ee03fb18"_hash, 1155876); //mediantime: ??????????. New rules activated in this block.

        checkpoints.emplace_back("00000000000025c23a19cc91ad8d3e33c2630ce1df594e1ae0bf0eabe30a9176"_hash, 1200000);
        checkpoints.emplace_back("000000007ec390190c60b5010a8ea14f5ce53e35be684eacc36486fec3b34744"_hash, 1300000);
        checkpoints.emplace_back("000000000000fce208da3e3b8afcc369835926caa44044e9c2f0caa48c8eba0f"_hash, 1400000);
        checkpoints.emplace_back("0000000000049a6b07f91975568dc96bb1aec1a24c6bdadb21eb17c9f1b7256f"_hash, 1500000);

    } else if (network == domain::config::network::mainnet) {
        // BTC Mainnet
        checkpoints.reserve(36);
        checkpoints.emplace_back("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash, 0);
        checkpoints.emplace_back("0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d"_hash, 11111);
        checkpoints.emplace_back("000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6"_hash, 33333);
        checkpoints.emplace_back("00000000001e1b4903550a0b96e9a9405c8a95f387162e4944e8d9fbe501cd6a"_hash, 68555);
        checkpoints.emplace_back("00000000006a49b14bcf27462068f1264c961f11fa2e0eddd2be0791e1d4124a"_hash, 70567);
        checkpoints.emplace_back("0000000000573993a3c9e41ce34471c079dcf5f52a0e824a81e7f953b8661a20"_hash, 74000);
        checkpoints.emplace_back("00000000000291ce28027faea320c8d2b054b2e0fe44a773f3eefb151d6bdc97"_hash, 105000);
        checkpoints.emplace_back("000000000000774a7f8a7a12dc906ddb9e17e75d684f15e00f8767f9e8f36553"_hash, 118000);
        checkpoints.emplace_back("00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe"_hash, 134444);
        checkpoints.emplace_back("000000000000033b512028abb90e1626d8b346fd0ed598ac0a3c371138dce2bd"_hash, 140700);
        checkpoints.emplace_back("000000000000099e61ea72015e79632f216fe6cb33d7899acb35b75c8303b763"_hash, 168000);
        checkpoints.emplace_back("000000000000059f452a5f7340de6682a977387c17010ff6e6c3bd83ca8b1317"_hash, 193000);
        checkpoints.emplace_back("000000000000048b95347e83192f69cf0366076336c639f9b7228e9ba171342e"_hash, 210000);
        checkpoints.emplace_back("00000000000001b4f4b433e81ee46494af945cf96014816a4e2370f11b23df4e"_hash, 216116);
        checkpoints.emplace_back("00000000000001c108384350f74090433e7fcf79a606b8e797f065b130575932"_hash, 225430);
        checkpoints.emplace_back("000000000000003887df1f29024b06fc2200b55f8af8f35453d7be294df2d214"_hash, 250000);
        checkpoints.emplace_back("0000000000000001ae8c72a0b0c301f67e3afca10e819efa9041e458e9bd7e40"_hash, 279000);
        checkpoints.emplace_back("00000000000000004d9b4ef50f0f9d686fd69db2e03af35a100370c64632a983"_hash, 295000);
        checkpoints.emplace_back("000000000000000082ccf8f1557c5d40b21edabb18d2d691cfbf87118bac7254"_hash, 300000);
        checkpoints.emplace_back("00000000000000000409695bce21828b31a7143fa35fcab64670dd337a71425d"_hash, 325000);
        checkpoints.emplace_back("0000000000000000053cf64f0400bb38e0c4b3872c38795ddde27acb40a112bb"_hash, 350000);
        checkpoints.emplace_back("000000000000000009733ff8f11fbb9575af7412df3fae97f382376709c965dc"_hash, 375000);
        checkpoints.emplace_back("000000000000000004ec466ce4732fe6f1ed1cddc2ed4b328fff5224276e3f6f"_hash, 400000);
        checkpoints.emplace_back("00000000000000000142adfebcb9a0aa75f0c4980dd5c7dd17062bf7de77c16d"_hash, 425000);
        checkpoints.emplace_back("0000000000000000014083723ed311a461c648068af8cef8a19dcd620c07a20b"_hash, 450000);
        checkpoints.emplace_back("0000000000000000017c42fd88e78ab02c5f5c684f8344e1f5c9e4cebecde71c"_hash, 475000);

        //2017-Aug Upgrade - Bitcoin Cash UAHF (1501590000)
        checkpoints.emplace_back("000000000000000000eb9bc1f9557dc9e2cfe576f57a52f6be94720b338029e4"_hash, 478557);  //mediantime: 1501589769
        checkpoints.emplace_back("0000000000000000011865af4122fe3b144e2cbeea86142e8ff2fb4107352d43"_hash, 478558);  //mediantime: 1501591048. New rules activated in the next block.
        checkpoints.emplace_back("00000000000000000019f112ec0a9982926f1258cdcc558dd7c3b7e5dc7fa148"_hash, 478559);  //mediantime: ??????????. New rules activated in this block.

        checkpoints.emplace_back("00000000000000000024fb37364cbf81fd49cc2d51c09c75c35433c3a1945d04"_hash, 500000);
        checkpoints.emplace_back("0000000000000000003ca88d20895d2535f304cca8afb08e7e5503fcac1da752"_hash, 515000);
        checkpoints.emplace_back("000000000000000000024e9be1c7b56cab6428f07920f21ad8457221a91371ae"_hash, 530000);
        checkpoints.emplace_back("0000000000000000001f1256c05b66eb061e3fc942521b146dd136a630947b17"_hash, 545000);
        checkpoints.emplace_back("0000000000000000002c7b276daf6efb2b6aa68e2ce3be67ef925b3264ae7122"_hash, 560000);
        checkpoints.emplace_back("00000000000000000007df59824a0c86d1cc21b90eb25259dd2dba5170cea5f5"_hash, 575000);
        checkpoints.emplace_back("000000000000000000061610767eaa0394cab83c70ff1c09dd6b2a2bdad5d1d1"_hash, 590000);
        // checkpoints.emplace_back("", 605000);

    } else {
        // BCH Regtest
        checkpoints.reserve(1);
        checkpoints.emplace_back("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"_hash, 0);    //TODO(fernando): check if BTC-Regtest-genesis == BCH-Regtest-genesis
    }
#endif //defined(KTH_CURRENCY_BCH)

    return checkpoints;
}

} // namespace kth::domain::config
