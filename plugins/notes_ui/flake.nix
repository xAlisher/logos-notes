{
  description = "Logos Notes UI plugin";
  inputs.logos-module-builder.url = "github:logos-co/logos-module-builder";
  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
