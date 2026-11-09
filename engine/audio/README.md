# engine/audio

The game's sound without platform dependencies (replaces `Maxsound.dll`): files of the game
installation (`sound_bank.hpp`), the music of content packages (`package_music.hpp`) and the game's
sounds (`game_audio.hpp`: sounds on screen, voice messages, music). Decoding WAVE files (including
the 4-bit music), Ogg Vorbis and FLAC, and the software mixer are `annokit::audio`'s. The device is
`annokit::platform::AudioOutput`; the viewer connects the two in `engine/app/src/game_sound.*`.

Description: [docs/formats/wav.md](../../docs/formats/wav.md).
Videos (Smacker) are decoded by `annokit::video`.
