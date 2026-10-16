class Mdt < Formula
  desc "Terminal Markdown reader: typeset maths, images, syntax highlighting"
  homepage "https://github.com/wudream813/markdown-in-terminal"
  version "0.3.15"
  license "MIT"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and a fully
  # static x86-64 build on Linux (no glibc version to satisfy).
  on_macos do
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.15/mdt-macos-arm64"
      sha256 "374fc2c2b2bc4ccc0bcef66bf6983cb3433986428922200a096874e442caf3ad"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.15/mdt-linux-amd64"
      sha256 "21213705f08035b15f8e39133be0586f7702095308a273899ddc80afeb1d2645"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.15/docs/mdt.1"
    sha256 "84b40b08162441229c042cd9f86ee7fa6beb7c4d013de12c63c59496f36e98c0"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.15/completions/mdt.bash"
    sha256 "2ef8c687a3c17e67c16d8fd50104efdd363ce1c4b6a51eba74dc035b43e0439d"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.15/completions/_mdt"
    sha256 "0d46d75e85ce6b35c1c08457da8e166ad8c38595ea40619e3b98c3a332fe0f70"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.15/completions/mdt.fish"
    sha256 "6b5bf1b53ef2cd41afe8c0e81e97fd388a6c1fac9dd4b3558a9f295a6781a5c0"
  end

  def install
    binary = OS.mac? ? "mdt-macos-arm64" : "mdt-linux-amd64"
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
