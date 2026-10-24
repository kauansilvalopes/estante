#include "../src/emulator_import.hpp"
#include <cassert>
#include <cstring>
#include <vector>

int main()
{
    char game[200]{}, cover[200]{}, url[768]{};
    assert(estante::make_swan_paths("/data/SwanStationPS5/games", "/data/SwanStationPS5/covers/boxart",
                                    "Pokémon & café #1", game, sizeof game, cover, sizeof cover));
    assert(std::strcmp(game, "/data/SwanStationPS5/games/Pokémon & café #1") == 0);
    assert(std::strcmp(cover, "/data/SwanStationPS5/covers/boxart/Pok__mon___caf____1.png") == 0);

    // The emulator cuts the id at 63 characters before adding ".png".
    char title80[81];
    std::memset(title80, 'a', 80);
    title80[80] = 0;
    char file[128]{};
    assert(estante::swan_cover_filename(title80, file, sizeof file));
    assert(std::strlen(file) == 63 + 4 && std::strcmp(file + 63, ".png") == 0);

    // Match SwanStationPS5's clean_title before finish_game derives the id.
    assert(estante::swan_cover_filename("Crash_[USA]_[SLUS-12345]", file, sizeof file));
    assert(std::strcmp(file, "Crash.png") == 0);
    assert(estante::swan_cover_filename("Mega___Man [U] (Disc 1)", file, sizeof file));
    assert(std::strcmp(file, "Mega_Man.png") == 0);
    assert(estante::swan_cover_filename("Crash_Bandicoot.CHD", file, sizeof file));
    assert(std::strcmp(file, "Crash_Bandicoot.png") == 0);


    char title[192]{};
    struct Case { const char *file, *want; };
    const Case cases[] = {
        {"Mario Kart - Double Dash!! (USA).rvz", "Mario Kart - Double Dash!!"},
        {"Legend of Zelda, The - The Wind Waker (USA).rvz", "The Legend of Zelda - The Wind Waker"},
        {"Simpsons, The - Hit & Run (USA).rvz", "The Simpsons - Hit & Run"},
        {"Super Mario Galaxy (USA) (En,Fr,Es).rvz", "Super Mario Galaxy"},
        {"Mortal Kombat - Armageddon (Europe) (En,Fr,De,Es,It).rvz", "Mortal Kombat - Armageddon"},
        {"Eternal Darkness - Sanity_s Requiem (USA).gcz", "Eternal Darkness - Sanity_s Requiem"},
        {"New Super Mario Bros. Wii (Europe) (En,Fr,De,Es,It) (Rev 1).rvz", "New Super Mario Bros. Wii"},
        {"Pokémon Colosseum [!]  (USA).iso", "Pokémon Colosseum"},
        {"Game, A.rvz", "A Game"},
    };
    for (const Case &c : cases)
    {
        assert(estante::clean_rom_title(c.file, title, sizeof title));
        assert(std::strcmp(title, c.want) == 0);
    }
    assert(!estante::clean_rom_title("(USA).rvz", title, sizeof title)); // nothing readable left
    assert(!estante::clean_rom_title("x.rvz", title, 1));                // no room
    assert(estante::meaningless_title("0123456789ABCDEF") && estante::meaningless_title("ab") &&
           !estante::meaningless_title("Mario Party 4"));

    char edge[300]{};
    assert(estante::clean_rom_title("Dr. Mario.rvz", edge, sizeof edge) &&
           std::strcmp(edge, "Dr. Mario") == 0);
    assert(estante::clean_rom_title("No extension", edge, sizeof edge) &&
           std::strcmp(edge, "No extension") == 0);
    assert(estante::clean_rom_title("Title.longext", edge, sizeof edge) &&
           std::strcmp(edge, "Title.longext") == 0);
    assert(estante::clean_rom_title("A\t  B.rvz", edge, sizeof edge) &&
           std::strcmp(edge, "A B") == 0);
    assert(estante::clean_rom_title("Game, Asterix.rvz", edge, sizeof edge) &&
           std::strcmp(edge, "Game, Asterix") == 0);
    assert(estante::clean_rom_title("Foo, The - Bar, The.rvz", edge, sizeof edge) &&
           std::strcmp(edge, "The Foo - Bar, The") == 0);
    std::memset(edge, 'x', 250);
    edge[250] = 0;
    char edge_out[256]{};
    assert(estante::clean_rom_title(edge, edge_out, sizeof edge_out) && std::strlen(edge_out) == 250);
    assert(!estante::clean_rom_title("(only tags) [USA].rvz", edge_out, sizeof edge_out));

    // Deterministic fuzz of short arbitrary byte strings, including malformed UTF-8 and brackets.
    unsigned rng = 0x53a19u;
    for (int sample = 0; sample < 5000; ++sample)
    {
        char input[256], output[256];
        const std::size_t size = sample % sizeof input;
        for (std::size_t i = 0; i < size; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            static const unsigned char special[] = {'(', ')', '[', ']', '.', ',', '\t', 0x80, 0xff};
            input[i] = static_cast<char>((rng & 3u) ? (rng >> 24) : special[(rng >> 8) % sizeof special]);
            if (input[i] == 0)
                input[i] = 'x';
        }
        input[size] = 0;
        const bool ok = estante::clean_rom_title(input, output, sizeof output);
        if (ok)
            assert(std::strlen(output) < sizeof output);
    }


    {
        // PlayStation boot serial out of the start of a disc image, in the two layouts found in the wild.
        auto build = [](bool raw, const char *boot, std::size_t cnf_lba, std::vector<unsigned char> &image)
        {
            const std::size_t sector = raw ? 2352 : 2048, offset = raw ? 24 : 0;
            image.assign((cnf_lba + 2) * sector, 0);
            if (raw) // every raw sector starts with the sync pattern and a mode byte
                for (std::size_t base = 0; base < image.size(); base += sector)
                {
                    static const unsigned char sync[12] = {0, 255, 255, 255, 255, 255,
                                                           255, 255, 255, 255, 255, 0};
                    std::memcpy(&image[base], sync, 12);
                    image[base + 15] = 2;
                }
            auto put = [&](std::size_t lba, const unsigned char *data, std::size_t length)
            { std::memcpy(&image[lba * sector + offset], data, length); };
            unsigned char pvd[2048]{};
            pvd[0] = 1;
            std::memcpy(pvd + 1, "CD001", 5);
            pvd[158] = 20; // root directory at sector 20, one sector long
            pvd[166] = 0;
            pvd[167] = 8;
            put(16, pvd, sizeof pvd);
            unsigned char dir[2048]{};
            std::size_t pos = 0;
            auto entry = [&](const char *name, std::size_t lba)
            {
                const std::size_t n = std::strlen(name);
                dir[pos] = static_cast<unsigned char>(34 + n + (n % 2 == 0 ? 1 : 0));
                dir[pos + 2] = static_cast<unsigned char>(lba);
                dir[pos + 3] = static_cast<unsigned char>(lba >> 8);
                dir[pos + 4] = static_cast<unsigned char>(lba >> 16);
                dir[pos + 32] = static_cast<unsigned char>(n);
                std::memcpy(dir + pos + 33, name, n);
                pos += dir[pos];
            };
            entry("README.TXT;1", 30);
            entry("SYSTEM.CNF;1", cnf_lba);
            put(20, dir, sizeof dir);
            unsigned char cnf[2048]{};
            std::memcpy(cnf, boot, std::strlen(boot));
            put(cnf_lba, cnf, sizeof cnf);
        };
        std::vector<unsigned char> image;
        char serial[16]{};
        build(true, "BOOT = cdrom:\\SLUS_006.84;1\r\nTCB = 4\r\n", 24, image);
        assert(estante::ps1_boot_serial(image.data(), image.size(), serial, sizeof serial) &&
               std::strcmp(serial, "SLUS-00684") == 0);
        build(false, "BOOT=cdrom:SCES_035.43;1\n", 24, image);
        assert(estante::ps1_boot_serial(image.data(), image.size(), serial, sizeof serial) &&
               std::strcmp(serial, "SCES-03543") == 0);
        build(true, "BOOT = cdrom:\\slps-00355;1", 24, image);
        assert(estante::ps1_boot_serial(image.data(), image.size(), serial, sizeof serial) &&
               std::strcmp(serial, "SLPS-00355") == 0);
        // Only the start of the image was read: SYSTEM.CNF further in is "not found", not garbage.
        build(true, "BOOT = cdrom:\\SLUS_006.84;1", 200, image);
        assert(!estante::ps1_boot_serial(image.data(), 100 * 2352, serial, sizeof serial));
        assert(estante::ps1_boot_serial(image.data(), image.size(), serial, sizeof serial));
        // Not a disc, truncated, without a BOOT line, or no room for the answer.
        unsigned char zeros[4096]{};
        assert(!estante::ps1_boot_serial(zeros, sizeof zeros, serial, sizeof serial));
        assert(!estante::ps1_boot_serial(image.data(), 10, serial, sizeof serial));
        build(false, "TCB = 4\r\n", 24, image);
        assert(!estante::ps1_boot_serial(image.data(), image.size(), serial, sizeof serial));
        build(false, "BOOT = cdrom:\\SLUS_006.84;1", 24, image);
        assert(!estante::ps1_boot_serial(image.data(), image.size(), serial, 4));
    }

    char long_name[220];
    std::memset(long_name, 'x', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = 0;
    std::strcpy(game, "unchanged");
    assert(!estante::make_swan_paths("/data/SwanStationPS5/games", "/data/SwanStationPS5/covers/boxart",
                                     long_name, game, sizeof game, cover, sizeof cover));
    assert(std::strcmp(game, "unchanged") == 0); // no silently truncated game path

    assert(estante::encode_fs_request("/api/fs/download?path=/data/A B& C#1/Pokémon.bin", url,
                                      sizeof url));
    assert(std::strcmp(url, "/api/fs/download?path=/data/A%20B%26%20C%231/Pok%C3%A9mon.bin") == 0);
    assert(estante::encode_fs_request("/api/fs/download?path=/data/A%20B/C%23.bin", url,
                                      sizeof url));
    assert(std::strcmp(url, "/api/fs/download?path=/data/A%20B/C%23.bin") == 0);
    assert(estante::encode_fs_request("/api/fs/download?path=/data/100%done", url, sizeof url));
    assert(std::strcmp(url, "/api/fs/download?path=/data/100%25done") == 0);
    assert(!estante::encode_fs_request("/api/fs/download?path=/data/too-long", url, 8));
    char request[1024]{};
    assert(estante::build_http_request("/api/state", request, sizeof request));
    assert(std::strcmp(request,
                       "GET /api/state HTTP/1.0\r\nHost: 127.0.0.1:6969\r\nConnection: close\r\n\r\n") == 0);
    char too_long_path[1100];
    std::memset(too_long_path, 'a', sizeof too_long_path - 1);
    too_long_path[sizeof too_long_path - 1] = 0;
    assert(!estante::build_http_request(too_long_path, request, sizeof request));

    bool limit_reached = false;
    int imported = 0;
    for (int i = 0; i < 1026; ++i)
        if (estante::import_slot_available(i, 1024, limit_reached))
            ++imported;
    assert(imported == 1024 && limit_reached);
    assert(estante::take_limit_warning(limit_reached));
    assert(!estante::take_limit_warning(limit_reached));
}
