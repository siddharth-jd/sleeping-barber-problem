from flask import Flask, request, jsonify, send_from_directory

PROC_ENTRY = "/proc/sleeping_barber_cop"

app = Flask(__name__)

# Function to read the current status of the simulation
def read_status():
    try:
        with open(PROC_ENTRY, "r") as f:
            return f.read()
    except Exception as e:
        return f"Error: {e}"

# Function to write a command to the simulation
def write_command(command):
    try:
        with open(PROC_ENTRY, "w") as f:
            f.write(command)
        return "Command executed successfully."
    except Exception as e:
        return f"Error: {e}"

@app.route("/status", methods=["GET"])
def get_status():
    status = read_status()
    return jsonify({"status": status})

@app.route("/command", methods=["POST"])
def execute_command():
    data = request.json
    command = data.get("command", "")
    response = write_command(command)
    return jsonify({"response": response})

# Route for serving the HTML frontend at the root URL
@app.route("/")
def serve_frontend():
    return send_from_directory("static", "sleeping_barber_page.html")

# Route for serving the favicon
@app.route("/favicon.ico")
def serve_favicon():
    return send_from_directory("static", "favicon.ico")

# Route for serving static files including CSS
@app.route("/static/<path:filename>")
def serve_static(filename):
    return send_from_directory("static", filename)

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
