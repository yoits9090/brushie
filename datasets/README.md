# Benchmark data

The checked-out Kodak suite is the 24-image Kodak Lossless True Color Image
Suite mirrored from [MohamedBakrAli/Kodak-Lossless-True-Color-Image-Suite](https://github.com/MohamedBakrAli/Kodak-Lossless-True-Color-Image-Suite), whose README points to the original Kodak test suite. The DIV2K files are the official `DIV2K_valid_LR_bicubic_X2.zip` validation archive from [ETH CVL](https://data.vision.ee.ethz.ch/cvl/DIV2K/).

`scripts/dataset_benchmark.py` uses all 24 Kodak images and the first ten
DIV2K validation images, resizing each to 512² and 1024² before the timed
resident-buffer encode call. The original archives are not required to run the
codec itself; keep the dataset folders out of distribution if licensing or
storage policy requires it.
