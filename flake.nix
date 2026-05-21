{
  description = "Logos Notes — encrypted local-first notes module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";

    # Follow the builder's nixpkgs to avoid Qt ABI mismatches
    nixpkgs.follows = "logos-module-builder/nixpkgs";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
