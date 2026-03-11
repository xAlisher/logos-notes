{
  description = "Logos Notes — encrypted local-first notes app (Phase 0)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        nativeBuildInputs = with pkgs; [
          cmake
          ninja
          pkg-config
          qt6.wrapQtAppsHook
        ];
        buildInputs = with pkgs; [
          qt6.qtbase
          qt6.qtdeclarative      # QtQml + QtQuick
          qt6.qtquickcontrols2
          libsodium
        ];
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "logos-notes";
          version = "0.1.0";
          src = ./.;
          inherit nativeBuildInputs buildInputs;
          cmakeFlags = [
            "-GNinja"
            "-DCMAKE_BUILD_TYPE=Release"
          ];
        };

        devShells.default = pkgs.mkShell {
          inherit buildInputs;
          nativeBuildInputs = nativeBuildInputs ++ (with pkgs; [
            clang-tools   # clangd LSP
            gdb
          ]);
          shellHook = ''
            echo "logos-notes dev shell"
            echo "  Qt:        ${pkgs.qt6.qtbase.version}"
            echo "  libsodium: ${pkgs.libsodium.version}"
            echo ""
            echo "Build:"
            echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug"
            echo "  cmake --build build"
            echo "  ./build/logos-notes"
          '';
        };
      });
}
