{
  description = "Logos Notes — encrypted local-first notes app";

  inputs = {
    # Follow the same nixpkgs as logos-cpp-sdk to ensure Qt compatibility
    nixpkgs.follows = "logos-cpp-sdk/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-liblogos = {
      url = "github:logos-co/logos-liblogos";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    logos-design-system = {
      url = "github:logos-co/logos-design-system";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    };
  };

  outputs = { self, nixpkgs, flake-utils, logos-cpp-sdk, logos-liblogos, logos-design-system }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosHeaders = logos-liblogos.packages.${system}.default;
        designSystem = logos-design-system.packages.${system}.default;

        # Build libkeycard.so from status-keycard-go (Issue #44)
        libkeycard = pkgs.buildGoModule {
          pname = "libkeycard";
          version = "unstable-2024-03-31";

          src = pkgs.fetchFromGitHub {
            owner = "status-im";
            repo = "status-keycard-go";
            rev = "76c880480c62dbf0ee67ee342f87ab80a928ed73";
            hash = "sha256-AcTMJm7aGSuh0emH+3Vun/BOdtC7ntwQVbakbKkrbFA=";
          };

          # Disable vendorHash since we're building a CGO shared library
          vendorHash = null;

          buildInputs = [ pkgs.pcsclite ];
          nativeBuildInputs = [ pkgs.pkg-config ];

          # Build only the shared library from the shared/ directory
          buildPhase = ''
            cd shared
            export CGO_ENABLED=1
            go build -buildmode=c-shared -o libkeycard.so .
          '';

          installPhase = ''
            mkdir -p $out/lib
            cp libkeycard.so $out/lib/
            cp libkeycard.h $out/lib/
          '';
        };

        nativeBuildInputs = with pkgs; [
          cmake
          ninja
          pkg-config
          qt6.wrapQtAppsHook
        ];
        buildInputs = with pkgs; [
          qt6.qtbase
          qt6.qtdeclarative
          qt6.qtremoteobjects
          libsodium
          libkeycard
        ];
      in
      {
        # Full standalone app
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "logos-notes";
          version = "1.0.0";
          src = ./.;
          inherit nativeBuildInputs buildInputs;
          cmakeFlags = [
            "-GNinja"
            "-DCMAKE_BUILD_TYPE=Release"
          ];
          preConfigure = ''
            export LOGOS_LIBLOGOS_HEADERS="${logosHeaders}/include"
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
          '';
          # QML_IMPORT_PATH so Logos.Theme resolves at build and runtime
          QML_IMPORT_PATH = "${designSystem}/lib";
        };

        # Core module for nix-bundle-lgx: lib/notes_plugin.so + lib/metadata.json
        packages.lib = pkgs.stdenv.mkDerivation {
          pname = "logos-notes-core";
          version = "1.0.0";
          src = ./.;
          inherit nativeBuildInputs buildInputs;
          cmakeFlags = [
            "-GNinja"
            "-DCMAKE_BUILD_TYPE=Release"
          ];
          preConfigure = ''
            export LOGOS_LIBLOGOS_HEADERS="${logosHeaders}/include"
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
          '';
          # Only build the plugin target — skip standalone app (needs QML imports)
          buildPhase = ''
            cmake --build . --target notes_plugin -j$NIX_BUILD_CORES
          '';
          installPhase = ''
            mkdir -p $out/lib
            cp notes_plugin.so $out/lib/
            cp ${./metadata.json} $out/lib/metadata.json
          '';
        };

        # UI plugin for nix-bundle-lgx: lib/Main.qml + lib/metadata.json
        # src points to plugins/notes_ui/ so the bundler finds metadata.json there.
        packages.ui = pkgs.stdenv.mkDerivation {
          pname = "logos-notes-ui";
          version = "1.0.0";
          src = ./plugins/notes_ui;
          dontBuild = true;
          dontConfigure = true;
          installPhase = ''
            mkdir -p $out/lib
            cp Main.qml $out/lib/
            cp metadata.json $out/lib/
          '';
        };

        devShells.default = pkgs.mkShell {
          inherit buildInputs;
          nativeBuildInputs = nativeBuildInputs ++ (with pkgs; [
            clang-tools
            gdb
          ]);
          QML_IMPORT_PATH = "${designSystem}/lib";
          shellHook = ''
            echo "logos-notes dev shell"
            echo "  Qt:        ${pkgs.qt6.qtbase.version}"
            echo "  libsodium: ${pkgs.libsodium.version}"
            echo "  Design:    ${designSystem}"
            echo ""
            echo "Build:"
            echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug"
            echo "  cmake --build build"
            echo "  ./build/logos-notes"
            echo ""
            echo "LGX packaging:"
            echo "  nix bundle --bundler github:logos-co/nix-bundle-lgx .#lib"
            echo "  nix bundle --bundler github:logos-co/nix-bundle-lgx .#ui"
          '';
        };
      });
}
