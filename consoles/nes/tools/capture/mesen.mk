# Read after MesenCE's makefile, from its isolated build tree.
# Only emulator code and software capture: no InteropDLL, SDL, X11, or MacOS.
.PHONY: nnes-headless
nnes-headless: $(COREOBJ) $(UTILOBJ) $(LUAOBJ) $(SEVENZIPOBJ)
	$(CXX) $(CXXFLAGS) $(NNES_HOST) $(COREOBJ) $(UTILOBJ) $(LUAOBJ) $(SEVENZIPOBJ) -pthread $(NNES_LINK) -o nnes-mesen
