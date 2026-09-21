// THIS FILE IS GENERATED. DO NOT EDIT.
// Source: MUnique.OpenMU.Network.Packets v0.9.9 (NuGet)
// Generator: tools/gen_wire_sizes.py
//
// Static-assert that each client packet struct fits within the wire packet
// length declared by OpenMU. Guards against the PR #402 class of bug where a
// dropped #pragma pack(1) silently inflates the client struct above the wire
// size, causing safe_cast to reject every packet (freezes the loading screen).

#pragma once

static_assert(sizeof(PRECEIVE_CREATE_CHARACTER) <= 42,
              "wire size drift -- generated from CharacterCreationSuccessful (42 bytes)");

static_assert(sizeof(PRECEIVE_JOIN_MAP_SERVER_EXTENDED) <= 96,
              "wire size drift -- generated from CharacterInformationExtended (96 bytes)");

static_assert(sizeof(PRECEIVE_REVIVAL_EXTENDED) <= 36,
              "wire size drift -- generated from RespawnAfterDeathExtended (36 bytes)");
