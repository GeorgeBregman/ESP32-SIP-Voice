import subprocess, urllib.request, json, zipfile, io

def get_token():
    try:
        proc = subprocess.Popen(['git', 'credential', 'fill'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        out, _ = proc.communicate(input="protocol=https\nhost=github.com\n\n")
        for line in out.split('\n'):
            if line.startswith('password='):
                return line.split('=', 1)[1].strip()
    except Exception as e:
        print("Could not get token:", e)
    return None

def main():
    token = get_token()
    if not token:
        print("No token found")
        return

    url = 'https://api.github.com/repos/GeorgeBregman/ESP32-SIP-Voice/actions/runs/27029142235/logs'
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0', 'Authorization': f'token {token}'})
    try:
        with urllib.request.urlopen(req) as response:
            with zipfile.ZipFile(io.BytesIO(response.read())) as z:
                for name in z.namelist():
                    if 'esp32s3' in name.lower() or 'firmware' in name.lower():
                        if 'Build for' in name or 'esp' in name:
                            print(f'=== {name} ===')
                            content = z.read(name).decode()
                            lines = content.split('\n')
                            # Look for "error:"
                            error_lines = []
                            for i, line in enumerate(lines):
                                if 'error:' in line.lower() or 'failed' in line.lower():
                                    error_lines.extend(lines[max(0, i-5):min(len(lines), i+5)])
                            if error_lines:
                                print('\n'.join(error_lines[-50:]))
                            else:
                                print('\n'.join(lines[-30:]))
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
