# Install Zigma Free

Use the installation wizard to set up Zigma Free on a Zigma Spark-980. The wizard checks the installed firmware before proceeding with the full installation. If it does not recognise the firmware, it may stop after setting up SSH.

## Before you start

- The robot needs a 2.4 GHz network and must be able to reach the computer. Allow inbound TCP port 8000 for Python on your home network.
- Download the deployment archive from [GitHub Releases](https://github.com/bultza/zigma-spark-980-free/releases) and extract the entire archive.
- Use a Windows 10/11 or Linux computer with Wi-Fi and Python 3.10 or later. On Linux, you also need NetworkManager (`nmcli`) and Python's `venv` and `pip`. On Debian or Ubuntu, install `python3-venv` if Python cannot create an environment or bootstrap pip.
- If Windows blocks `start-windows.cmd`, right-click it, open **Properties → General**, and select **Unblock** under **Security** if that option appears.
- Connect your computer to the Wi-Fi network the robot will use. We are going to emulate the Android app's pairing process, so the wizard needs your computer's IP address on that network before we start. The robot will connect to your Wi-Fi and download the installation files from your computer.
- The wizard needs internet access on its first run to install Python dependencies. The robot does not need internet access to install Zigma Free. If your router lets you block internet access for the robot while keeping it reachable on your home network, you can do that before installation to avoid contact with the old cloud. The wizard redirects known vendor hostnames in the robot’s `/etc/hosts`. On a robot without SSH, this happens after its first connection to your home Wi-Fi. Direct IP connections are still possible.

## Run the wizard

After extracting the deployment archive, open its top-level folder. The Windows and Linux launchers are there.

1. On Windows, double-click `start-windows.cmd`. On Linux, open a terminal in the extracted folder and run `sh start-linux.sh`. Run the wizard as your regular user.
2. Choose **New installation** if the wizard offers a choice.
3. Give your robot a nice name.
4. Enter the name of your Wi-Fi network, or press Enter to use the detected network. Enter its password when prompted. The password is visible while you type.
5. When the wizard asks, hold the robot's power button for 10 seconds to enter pairing mode.
6. Connect the computer to the robot's Wi-Fi access point, named `ROBOT###`.
7. The wizard sends the pairing settings and reconnects the computer to its saved home network. The robot's access point disappears as the robot joins your home Wi-Fi.
8. Give the robot about a minute to contact the wizard and download the installation files. Leave the wizard running while it waits.
9. The wizard enables SSH, verifies key-based access, and downloads a backup of the robot's 32 MiB flash. It checks the backup against a second read before installing Zigma Free. This backup already includes the SSH setup. If verification fails, retry or stop and resume later.
10. Wait for the local-mode checks to pass. If the wizard stops, see [Resume an interrupted installation](#resume-an-interrupted-installation).
11. Copy the displayed web access token and press Enter to open the dashboard in your web browser.
12. Sign in, then set a new password under **Settings → Web password**. Check that the dashboard connects and displays the robot's status.

## SSH and session files

The wizard saves the keys and backups in a session folder on your computer.

- **Windows** `%USERPROFILE%\Zigma\<session>`
- **Linux** `~/Zigma/<session>`

Things you will find in the folder:

- `connection.txt` with the SSH address and, after full installation, the dashboard URL.
- `zigma_key` and `zigma_key.pub`, your private and public SSH keys.
- `known_hosts`, the robot's saved SSH host key.
- `nor-backup.bin` and `nor-backup.json` if the flash backup was verified.
- `trial-failure-*.json` if the wizard recorded a failed activation trial.

Keep the private key and flash backup private. The flash backup contains credentials.

## Telegram setup

If you want to look up your robot's IP address when you're away from home, I recommend connecting it to Telegram. You can also get notifications and control the robot from there. The robot needs internet access to reach Telegram. The IP address reported by the bot is the robot’s local address.

1. In Telegram, go to [@BotFather](https://t.me/BotFather) and send `/newbot`, then follow the prompts. Create a separate bot for each robot. Copy the bot token it gives you and open a chat with your new bot.
2. In your robot's web interface, open **Settings → Telegram**, paste the bot token, and click **Save**.
3. When **Link chat** becomes available, click it and copy the command it shows.
4. Paste and send the command in your private chat with the bot. Once the chat is linked, click **Send test** in the web interface.
5. On the robot, enable **Receive commands** and click **Save**. Send `/getip` to look up the robot's local IP address or `/help` to see the available commands.

## Resume an interrupted installation

Run the same launcher and choose **Continue saved session** for your robot. Keep the original session folder. If the wizard asks you to restart the robot, follow its instructions and let it check that the robot is back online.

### If it still does not work

If SSH was already set up, you may still be able to connect. On your home Wi-Fi, use the SSH address in `connection.txt`. If the robot is unreachable there, put it in pairing mode, connect to its `ROBOT###` network, and try the pairing address if SSH is available there.

From inside the session folder, run this command with the appropriate IP address. The example uses the pairing address.

```sh
ssh -i ./zigma_key -o UserKnownHostsFile=./known_hosts -o StrictHostKeyChecking=yes root@192.168.99.1
```

If SSH reports that the host key is unknown or does not match, stop and verify the robot’s identity before connecting. If you still need help, open an issue in this repository with the wizard step and error message. If there is a `trial-failure-*.json` file, attach a copy after removing any private details. Keep your Wi-Fi password, tokens, private key, and flash backup out of the issue.

## Installer internals and build

See [Installer internals and build](BUILD.md) for details on the computer and robot scripts, compilation, and release packaging.
