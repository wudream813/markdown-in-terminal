class Mdt < Formula
  desc "Terminal Markdown reader: typeset maths, images, syntax highlighting"
  homepage "https://github.com/wudream813/markdown-in-terminal"
  version "0.3.17"
  license "MIT"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and fully
  # static builds on Linux for both x86-64 and arm64 (no glibc version to
  # satisfy either way).
  on_macos do
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.17/mdt-macos-arm64"
      sha256 "d62982125055a7dabe44bc941227bdb5389ed1a53d64d6d1040b761b8a2cae36"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.17/mdt-linux-amd64"
      sha256 "192ebb2b5c57c0a34a1993fd5cb9a0a95e535c566e2124aed97eaa40c163cf8d"
    end
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.17/mdt-linux-arm64"
      sha256 "4e8fbdbe7cd77e82930d96b404eadc3c47edd5fcadbc844efeb283d59039d0e3"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.17/docs/mdt.1"
    sha256 "84b40b08162441229c042cd9f86ee7fa6beb7c4d013de12c63c59496f36e98c0"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.17/completions/mdt.bash"
    sha256 "2ef8c687a3c17e67c16d8fd50104efdd363ce1c4b6a51eba74dc035b43e0439d"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.17/completions/_mdt"
    sha256 "0d46d75e85ce6b35c1c08457da8e166ad8c38595ea40619e3b98c3a332fe0f70"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.17/completions/mdt.fish"
    sha256 "6b5bf1b53ef2cd41afe8c0e81e97fd388a6c1fac9dd4b3558a9f295a6781a5c0"
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
