{
  description = "Logos Notes UI plugin";

  inputs = {
    # Pinned to the released v0.2.0 tag — provides mkLogosQmlModule + the codegen
    # (repc + modules().logos_beacon typed bridge) that notes_ui's QtRO backend needs.
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.0";

    # universal dep: notes_ui's QtRO backend reaches the Qt-free logos_beacon
    # (universal 2.0.0) via modules().logos_beacon.* (codegen). Legacy callModule
    # returns "null" for it (logos-notes#105). Points at beacon-basecamp main,
    # matching beacon_ui's own logos_beacon flake pin.
    logos_beacon.url = "git+file:///home/alisher/basecamp/modules/beacon-basecamp?ref=main";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
