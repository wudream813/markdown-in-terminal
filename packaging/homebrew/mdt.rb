class Mdt < Formula
  desc "Terminal Markdown reader: typeset maths, images, syntax highlighting"
  homepage "https://github.com/wudream813/markdown-in-terminal"
  version "0.3.19"
  license "MIT"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and fully
  # static builds on Linux for both x86-64 and arm64 (no glibc version to
  # satisfy either way).
  on_macos do
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.19/mdt-macos-arm64"
      sha256 "48f86e8ad6113c3d393d40b5af1f1a2797e3e51d7e0326ceee531d7fe2d17751"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.19/mdt-linux-amd64"
      sha256 "b91f274f91e441f02f70f7aef15aae73eb4a5a96f9b114a8b1740155016ae8ec"
    end
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.19/mdt-linux-arm64"
      sha256 "cb1e71522beb0eafc3b62989b6cea14ccbb4029bb5f7522c23c39c6f26e19c55"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.19/docs/mdt.1"
    sha256 "639201f846867c86da5644879d69b662b1200a83807d2ab55fe10acd14069b73"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.19/completions/mdt.bash"
    sha256 "9d62f8973e027aca34902a28566cb57527e72f7049688258b2546e495b022c9c"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.19/completions/_mdt"
    sha256 "0ee285fc87ed6d3c32c8a220d1dce48d16f58edbb1cd1a329975eae853fcdd55"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.19/completions/mdt.fish"
    sha256 "1c15834bfc9eb0ab38793381a57aed4717b77af8d23f1667a4c4ef9199f999b6"
  end

  def install
    binary = if OS.mac?
               "mdt-macos-arm64"
             elsif Hardware::CPU.arm?
               "mdt-linux-arm64"
             else
               "mdt-linux-amd64"
             end
    bin.install binary => "mdt"

    man1.install resource("man")
    bash_completion.install resource("completion-bash") => "mdt"
    zsh_completion.install resource("completion-zsh") => "_mdt"
    fish_completion.install resource("completion-fish") => "mdt.fish"
  end

  def caveats
    <<~EOS
      mdt is not notarised by Apple, so a copy that has been quarantined (that
      is, downloaded with a browser) will be refused by Gatekeeper.  Homebrew's
      own download is not quarantined; if you moved a copy in yourself, clear
      it with:

        xattr -d com.apple.quarantine #{bin}/mdt

      Remote images are fetched with the curl(1) command, and links are opened
      with xdg-open(1) (Linux), so having curl and xdg-utils installed helps.
    EOS
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/mdt --version")
    (testpath/"t.md").write "# heading\n\nsome *text*\n"
    assert_match "heading", shell_output("#{bin}/mdt --dump #{testpath}/t.md")
  end
end
