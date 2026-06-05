import urllib.request, json, time, sys

def fetch(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as response:
        return json.loads(response.read().decode())

def main():
    branch = "fix/lwip-werror-address"
    print(f"=== Checking runs for branch: {branch} ===")
    url = f"https://api.github.com/repos/GeorgeBregman/ESP32-SIP-Voice/actions/runs?branch={branch}&per_page=3"
    
    try:
        data = fetch(url)
        runs = data.get('workflow_runs', [])
        for r in runs:
            print(f"{r['id']} {r['head_sha'][:7]} {r['status']} {r['conclusion']}")
            
        if not runs:
            print("No runs found.")
            return

        run_id = runs[0]['id']
        print(f"\nTarget run id: {run_id}")
        
        status = ""
        for _ in range(10):
            run_info = fetch(f"https://api.github.com/repos/GeorgeBregman/ESP32-SIP-Voice/actions/runs/{run_id}")
            status = run_info.get('status')
            if status == 'completed':
                break
            print("Waiting for completion (10s)...")
            time.sleep(10)
            
        print(f"=== status: {status} ===")
        
        jobs_data = fetch(f"https://api.github.com/repos/GeorgeBregman/ESP32-SIP-Voice/actions/runs/{run_id}/jobs")
        for j in jobs_data.get('jobs', []):
            print(f"{j['name']} -> {j['conclusion']}")
            
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
