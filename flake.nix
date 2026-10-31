{
  description = "nativeshvc / nativekit development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (s: f s nixpkgs.legacyPackages.${s});
    in {
      devShells = forAll (system: pkgs:
        let
          inherit (pkgs) lib stdenv;
          darwin = stdenv.hostPlatform.isDarwin;
          linuxLibs = with pkgs; [
            xorg.libX11 xorg.libXext xorg.libXrandr xorg.libXi xorg.libXcursor
            libGL libGLU vulkan-loader alsa-lib
          ];
        in {
          # NoCC: on macOS the nix compiler cannot link Metal/AppKit binaries,
          # so the shell must not put a nix cc first. The system cc is used.
          default = pkgs.mkShellNoCC {
            packages = with pkgs; [
              cmake ninja python3 git gnumake glslang vulkan-headers vulkan-loader mise
              pkgsCross.mingwW64.buildPackages.gcc
            ]
            ++ lib.optionals darwin [ moltenvk ]
            ++ lib.optionals (!darwin) ([ gcc pkg-config ] ++ linuxLibs);

            shellHook =
              if darwin then ''
                export CC=/usr/bin/cc CXX=/usr/bin/c++
                unset LIBRARY_PATH
                export CMAKE_PREFIX_PATH="${pkgs.vulkan-headers}:${pkgs.vulkan-loader}:${pkgs.moltenvk}''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
                export VK_ICD_FILENAMES="${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json"
                export VK_DRIVER_FILES="$VK_ICD_FILENAMES"
                export DYLD_FALLBACK_LIBRARY_PATH="${pkgs.vulkan-loader}/lib:${pkgs.moltenvk}/lib''${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
              '' else ''
                export CMAKE_PREFIX_PATH="${pkgs.vulkan-headers}:${pkgs.vulkan-loader}''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
                export LD_LIBRARY_PATH="${lib.makeLibraryPath linuxLibs}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
              '';
          };
        });
    };
}
