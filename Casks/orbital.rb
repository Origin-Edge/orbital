cask "orbital" do
  version :latest
  sha256 :no_check

  url "https://github.com/Origin-Edge/orbital/releases/latest/download/Orbital-macos-arm64.dmg"
  name "Orbital"
  desc "Skill ve eklenti destekli, sade ve güçlü masaüstü AI kod asistanı"
  homepage "https://origin-edge.github.io/orbital"

  depends_on macos: ">= :monterey"

  app "Orbital.app"

  postflight do
    system_command "xattr",
                   args: ["-cr", "#{appdir}/Orbital.app"],
                   sudo: false
  end

  zap trash: [
    "~/.config/orbital",
    "~/.local/share/orbital",
  ]
end
