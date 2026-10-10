Fonts embedded in the HD UI (src/fr_hd_fonts.cpp is generated from these files).

* Poppins Medium / Bold — Indian Type Foundry, SIL Open Font License 1.1 (OFL.txt).
* DejaVuSans-Bold-symbols.ttf — subset (gender signs, arrows, triangles, star)
  of DejaVu Sans Bold, Bitstream Vera / DejaVu license (DejaVu-LICENSE.txt).

Regenerate: python3 snippet in the commit that added this file (bin2c of the
three .ttf files into frlg::hd::kPoppinsMedium / kPoppinsBold / kSymbols).
