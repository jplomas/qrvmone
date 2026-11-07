// qrvmone: Fast Quantum Resistant Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <test/state/host.hpp>

using namespace qrvmc;
using namespace qrvmc::literals;
inline constexpr auto addr = qrvmone::state::compute_new_account_address;

inline constexpr uint64_t nonces[] = {0, 1, 0x80, 0xffffffffffffffff};
inline constexpr address senders[] = {
    "Q00"_address, "Q01"_address, "Q000000000000000000000000000000000000000000000000000000008000000000000000000000000000000000000000"_address};
inline const bytes init_codes[] = {bytes{}, bytes{0xFE}};
inline constexpr bytes64 salts[] = {
    0x00_bytes64, 0xe75fb554e433e03763a1560646ee22dcb74e5274b34c5ad644e7c0f619a7e1d0_bytes64};

// Expected addresses match go-qrl crypto.CreateAddress (cyyber/go-qrl d4f0d2e):
// Keccak-512 over the "QRL-ADDR-v1" domain and rlp([sender, nonce]), keeping the
// full 64-byte digest as the address.
TEST(state_new_account_address, create)
{
    for (const auto& ic : init_codes)  // Init-code doesn't affect CREATE.
    {
        auto s = senders[0];
        EXPECT_EQ(addr(s, nonces[0], {}, ic),
            "Qced5630d8a39c22c399a6811280a2dda75b84c0b1494294e2de7ae1c98f27d553928bdd90407ce246402d10602b8291a39ba5c9baef8354cdefb686f973f9186"_address);
        EXPECT_EQ(addr(s, nonces[3], {}, ic),
            "Q69db1180c7978be6e5d2c04df0859b06c594efd40d87daa4e02f0ce86413809d4e4a93b90b2dec0e9fd73cbd626fdd71b89648f998caa56f87ddb2feedbdc772"_address);

        s = senders[1];
        EXPECT_EQ(addr(s, nonces[0], {}, ic),
            "Q568092bfaa4ff2135c8214c243931ec7489a31ce8f11bba215f6d46db92290db215dcc8fcc46ba7c53519efe0003f902a60f1c7e1048186aa5f14e28404868e7"_address);
        EXPECT_EQ(addr(s, nonces[1], {}, ic),
            "Q9f7b89c65c75d81c89da70494dff276e46ee969f7b89dbc63f1ef803d6c9156481feb764388317f39a99c0442aa00c5d822146428cd0cbef9b9b9cf2e70b4997"_address);
        EXPECT_EQ(addr(s, nonces[2], {}, ic),
            "Q7cdc4b427b0924a0b1f641cbae50ddf47eb067a173e2a9ae3daa19dc430ddbac4e6aef691cad1412f365686b672872f87d30caf525963965a40674f4793a0dbd"_address);
        EXPECT_EQ(addr(s, nonces[3], {}, ic),
            "Qeb9a5e08b49f24a5720c485ba374fdb01113eac1366103c0c2bfaf96574f42569b6b0a11eb09cb93f5c623617224349c4e2a7f72c26a5a22570133015dd84b97"_address);

        s = senders[2];
        EXPECT_EQ(addr(s, nonces[0], {}, ic),
            "Qd16f4f43475799bdaac380a872a9ff9b8a9f42d6e687cc14892a383b8a20699d699f99153d3b517b6ed2a2d92437f5ee4287df895569e6246c2c9b643c3eb6b9"_address);
        EXPECT_EQ(addr(s, nonces[3], {}, ic),
            "Q5f223a0f43bebe8352bd428d40c13135f3e65a4730add46b157fa80ee4b50f50f4932715317b8dbef2238322037b05fc88b2cbe070cebfc8cc3d0ea84e3ce761"_address);

        const auto beacon_deposit_address =
            addr("Q00000000000000000000000000000000000000000000000000000000b20a608c624Ca5003905aA834De7156C68b2E1d0"_address, 0, {}, ic);
        EXPECT_EQ(beacon_deposit_address,
            "Qa174895dcf754b696dac4f6374009d0563cb1c7e081469f0402e5e698e5b3bb7f73710a6811a4194a2417d4ffff949a942b1566a321aa7ec74ef95ae0ddc4349"_address);
    }
}

// Vectors from go-qrl crypto/crypto_test.go TestNewContractAddress.
TEST(state_new_account_address, create_go_qrl_vectors)
{
    constexpr auto sender =
        "Q43261cfab65ca3444b9a1001528c60d9a23d19bad2bc21051f24bf4bccd804dc63e35f36b63ee11ca7775ec832d6580929136989106db0231c6f04840cf8ee16"_address;
    EXPECT_EQ(addr(sender, 0, {}, {}),
        "Qf2a607fd56723c510a1bf4b000fd1e778aedc94e78fcc8cb18d531ec673212284520097bbed26e2df76ce4a5360c73add776771dcd5c0ce56751e30c2413c366"_address);
    EXPECT_EQ(addr(sender, 1, {}, {}),
        "Q5c2ef024f69257cab379fd37fe2dc6c9242320f8771b1a6d6c86b6cefadcfc449c473414b5647b4ee3361f28a7a4cd47b10b6c6fa3399d056f851d97998f7748"_address);
    EXPECT_EQ(addr(sender, 2, {}, {}),
        "Qb000412862016ae61f4003114cc4eaa6ba614ccbf8b096e5686a7df6305cb82743e72610275437d11f05758befb7ba44b17fb05f588615893d661e787cd0738e"_address);
}

// Expected addresses computed with go-qrl crypto.CreateAddress2 (cyyber/go-qrl
// d4f0d2e): Keccak-512 over the "QRL-ADDR-v1" domain and
// 0xff || sender || salt (64 bytes) || keccak256(init_code) (32 bytes).
TEST(state_new_account_address, create2)
{
    for (const auto n : nonces)  // Nonce doesn't affect CREATE2.
    {
        EXPECT_EQ(addr(senders[0], n, salts[0], init_codes[0]),
            "Q129e945d73c2abba8dc13b37c613c63c128e80ea7940b8a0479390faf8772f0c7cdd53065842db816b5e7609f45a56ff4d2971e75ed1e835619d09452a66832c"_address);

        EXPECT_EQ(addr(senders[2], n, salts[0], init_codes[1]),
            "Q0c860652fdaabeb42084b8bd9def6f4dbb5c7740e3657f187005c8175e883f85958bd27091188181a5d074254dda957e41101ab05cc977d8d9622d3eb70df0d8"_address);

        EXPECT_EQ(addr(senders[1], n, salts[1], init_codes[0]),
            "Qc33706436b546cbafad3470aa2d924ea6a319744854a4c493e43964a6002bc953724d5d8671d662f6f1efca46b40552d55fe1237318be9943e19da96952b754c"_address);

        EXPECT_EQ(addr(senders[2], n, salts[1], init_codes[1]),
            "Q7ee038a391652739674a824528794b848eeef5544f6035d10003e0b61d1cfd5596a73591d238fa9e806df1de24069517d8e4ecf5a2e9f1b4b44798c6eaa7adc2"_address);
    }
}
