# The weights

`kroko-en-q24-v1.bin`: Kroko's English streaming model in 2 and 4 bits, for the `pieasr` engine.

| | |
|---|---|
| Hugging Face | [BoumedineBillal/esp32-pie-asr-kroko-en](https://huggingface.co/BoumedineBillal/esp32-pie-asr-kroko-en), revision `d97c28a8` |
| Size | 26,651,392 bytes |
| CRC-32 | `bd3ff13c` (the firmware checks it at boot) |
| SHA-256 | `52673e74be9b44948640c6652ce8fd5b2b1b1faa4a66f3f8e43105405a38ce9f` |
| Format | `KRK1`: a header, a table of named tensors, the tensors 64-byte aligned (`pieasr/src/weights/weights_file.h`) |
| License | CC-BY-SA 4.0, from [Kroko](https://huggingface.co/Banafo/Kroko-ASR) by Banafo |

```bash
python models/download.py                          # into models/, checked
python models/download.py --card <the card's folder>   # and onto the microSD card's top folder
```

The firmware reads the file from the card's top folder and refuses another size or CRC. How it was made:
[docs/design.md](../docs/design.md#the-weights-2-and-4-bits).
