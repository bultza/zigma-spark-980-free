# Manual hack: get SSH on a Zigma Spark-980

This is the software-only way we got SSH on Caccamora. We use the factory Wi-Fi pairing service to run a small installation script. It adds your public key and starts Dropbear, the SSH server already on the robot.

This guide stops at SSH access. For the full Zigma Free setup, see [Install Zigma Free](../installer/README.md). For the original flash and UART work, see [How we hacked it](HISTORY.md).

## Before you start

- Use a Spark-980 with factory pairing firmware that accepts `script_url` and `script_md5`. The SSH bootstrap checks the robot's startup files and persistent storage before making changes.
- The commands below use a Linux computer with Python 3.10+, OpenSSH (`ssh` and `ssh-keygen`), OpenSSL, and `sh` as the server. The pairing computer needs Python 3 and a copy of the repository.
- Keep computer A on the home network to serve the script. Computer B joins the robot's `ROBOT###` access point to send the pairing request. You can also use one computer with Ethernet on the home network and Wi-Fi on the robot's access point.
- Use a 2.4 GHz Wi-Fi network the robot can join and from which it can reach computer A. Allow inbound TCP port 8000 to computer A on that network. Guest Wi-Fi with client isolation can block the download.

The factory client can contact its cloud service when it joins Wi-Fi. If your router lets you block internet access for the robot while keeping local access to computer A, you can apply that restriction before pairing. This SSH-only procedure does not run the full wizard's hostname protection or flash backup steps.

## 1. Generate the SSH key and serve the script

On computer A, from the repository root:

```sh
ssh-keygen -t ecdsa -b 256 -f "$HOME/zigma-ssh"
python3 installer/computer/installer.py \
  --profile ssh \
  --pubkey "$HOME/zigma-ssh.pub" \
  --host 192.168.1.75 \
  --output "$HOME/zigma-manual-session" \
  --serve
```

Replace `192.168.1.75` with computer A's IPv4 address on the home network. Choose a new output directory if the example already exists. The installer generates `install.sh` and prints the script URL. Leave the server running. It can serve the entry script over HTTP or HTTPS on the same port for the factory downloader.

Copy the generated `install.sh` and the printed URL to computer B. Keep the file and URL from the same run. The pairing sender hashes the exact bytes the server will deliver. Keep your private key on computer A.

## 2. Send the pairing request

Hold the robot's power button for about 10 seconds to enter factory pairing mode. Connect computer B to the `ROBOT###` access point. The robot's pairing service is normally at `http://192.168.99.1/`.

From the repository root on computer B, set your own SSID, script URL, and path to the copied file:

```sh
WIFI_SSID='YOUR_HOME_WIFI'
SCRIPT_URL='http://192.168.1.75:8000/install.sh'
SCRIPT_FILE='/path/to/copied/install.sh'

python3 installer/computer/send_pairing.py \
  --ssid "$WIFI_SSID" --app-type Redmond \
  --script-url "$SCRIPT_URL" --script-file "$SCRIPT_FILE"
```

Enter your home Wi-Fi password at the prompt. The first run previews the request with the password hidden. It calculates the checksum from the local file, so computer B can do this while connected only to the robot's access point.

If the preview looks right, run the same command with `--send` and enter the password again:

```sh
python3 installer/computer/send_pairing.py \
  --ssid "$WIFI_SSID" --app-type Redmond \
  --script-url "$SCRIPT_URL" --script-file "$SCRIPT_FILE" --send
```

The robot leaves pairing mode, joins your home network, and downloads the script. The access point may disappear before the pairing sender receives a reply. Check the server log before sending another request.

## 3. Verify SSH

Look for a request for `/install.sh` in computer A's server log. Use the source IP shown there, or find the robot in your router's DHCP leases. Give it about a minute to join your home network and start the download.

On computer A, connect with the private key you generated. Replace the example IP with the robot's address:

```sh
ssh -i "$HOME/zigma-ssh" -o IdentitiesOnly=yes root@192.168.1.28
```

On the first connection, check that the address belongs to your robot before accepting its host key. On subsequent connections, investigate any unexpected host-key change. Once logged in, run `id` to confirm the shell is running as root. You can then stop the installer server with **Ctrl+C**.

If the server never sees the download, check the advertised URL, TCP port 8000, and Wi-Fi isolation. If it sees the download but SSH stays closed, the download alone does not prove the bootstrap completed. The startup checks may have rejected the firmware or encountered an existing file that needs review.

## UART recovery

If the robot cannot join Wi-Fi and you can reach its serial pads, see [Getting the UART console back](HISTORY.md#getting-the-uart-console-back). That procedure uses a 3.3 V TTL adapter and the included Python console script. We verified the unlock on Ciccio.
