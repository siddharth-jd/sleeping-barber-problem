import re
import os
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
import argparse
from datetime import datetime

def parse_barber_log(log_file):
    """Parse the log file from the sleeping barber kernel module."""
    with open(log_file, 'r') as f:
        content = f.read()
    
    # Extract status information
    status_info = {}
    status_match = re.search(r'Status: (.*?)\nChairs occupied: (\d+)/(\d+)\nCustomers served: (\d+)\nCustomers lost: (\d+)\nCustomers remaining: (\d+)', content, re.DOTALL)
    if status_match:
        status_info = {
            'status': status_match.group(1),
            'chairs_occupied': int(status_match.group(2)),
            'max_chairs': int(status_match.group(3)),
            'customers_served': int(status_match.group(4)),
            'customers_lost': int(status_match.group(5)),
            'customers_remaining': int(status_match.group(6))
        }
    
    # Extract log entries
    log_pattern = r'\[(\s*\d+)\] (.*)'
    log_entries = re.findall(log_pattern, content)
    
    events = []
    for timestamp, message in log_entries:
        timestamp = int(timestamp.strip())
        events.append({'timestamp': timestamp, 'message': message})
    
    return status_info, events

def extract_customer_data(events):
    """Extract customer arrival, wait time, and service data from log entries."""
    customer_arrivals = {}
    customer_service_start = {}
    customer_service_end = {}
    customer_wait_times = {}
    service_times = {}
    
    for event in events:
        timestamp = event['timestamp']
        message = event['message']
        
        # Customer arrival
        arrival_match = re.search(r'Customer (\d+) arrived, waiting', message)
        if arrival_match:
            customer_id = int(arrival_match.group(1))
            customer_arrivals[customer_id] = timestamp
        
        # Service start
        service_start_match = re.search(r'Barber is cutting hair for customer (\d+) \(waited (\d+) ms\)', message)
        if service_start_match:
            customer_id = int(service_start_match.group(1))
            wait_time = int(service_start_match.group(2)) / 1000  # Convert ms to seconds
            customer_service_start[customer_id] = timestamp
            customer_wait_times[customer_id] = wait_time
        
        # Service time
        service_time_match = re.search(r'Cutting hair for customer (\d+) will take (\d+) ms', message)
        if service_time_match:
            customer_id = int(service_time_match.group(1))
            service_time = int(service_time_match.group(2)) / 1000  # Convert ms to seconds
            service_times[customer_id] = service_time
        
        # Service end
        service_end_match = re.search(r'Finished haircut for customer (\d+)', message)
        if service_end_match:
            customer_id = int(service_end_match.group(1))
            customer_service_end[customer_id] = timestamp
    
    return {
        'arrivals': customer_arrivals,
        'service_start': customer_service_start,
        'service_end': customer_service_end,
        'wait_times': customer_wait_times,
        'service_times': service_times
    }

def track_chairs_occupancy(events):
    """Track the number of occupied chairs over time."""
    occupancy_data = []
    chairs_occupied = 0
    
    for event in events:
        timestamp = event['timestamp']
        message = event['message']
        
        # Customer arrival increases occupancy
        if re.search(r'Customer \d+ arrived, waiting', message):
            chairs_occupied += 1
            occupancy_data.append((timestamp, chairs_occupied))
        
        # Service start decreases occupancy
        if re.search(r'Barber is cutting hair for customer \d+', message):
            chairs_occupied -= 1
            occupancy_data.append((timestamp, chairs_occupied))
        
        # Customer lost (shop full) doesn't change occupancy
        
    return occupancy_data

def track_barber_status(events):
    """Track the barber's status (sleeping/working) over time."""
    barber_status = []
    current_status = "sleeping"  # Barber starts sleeping
    
    for event in events:
        timestamp = event['timestamp']
        message = event['message']
        
        if "Barber going to sleep" in message:
            current_status = "sleeping"
            barber_status.append((timestamp, current_status))
        elif "Barber woke up" in message and not "going back to sleep" in message:
            current_status = "working"
            barber_status.append((timestamp, current_status))
    
    return barber_status

def generate_performance_graphs(status_info, customer_data, occupancy_data, barber_status, output_dir):
    """Generate performance graphs based on the parsed data."""
    os.makedirs(output_dir, exist_ok=True)
    
    # Plot 1: Customer wait times
    plt.figure(figsize=(10, 6))
    wait_times = list(customer_data['wait_times'].values())
    customer_ids = list(customer_data['wait_times'].keys())
    
    plt.bar(customer_ids, wait_times)
    plt.xlabel('Customer ID')
    plt.ylabel('Wait Time (seconds)')
    plt.title('Customer Wait Times')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'customer_wait_times.png'))
    plt.close()
    
    # Plot 2: Service times
    plt.figure(figsize=(10, 6))
    service_times = list(customer_data['service_times'].values())
    customer_ids = list(customer_data['service_times'].keys())
    
    plt.bar(customer_ids, service_times)
    plt.xlabel('Customer ID')
    plt.ylabel('Service Time (seconds)')
    plt.title('Customer Service Times')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'service_times.png'))
    plt.close()
    
    # Plot 3: Chair occupancy over time
    if occupancy_data:
        plt.figure(figsize=(12, 6))
        timestamps, occupancy = zip(*occupancy_data)
        
        # Convert timestamps to relative time in seconds
        start_time = min(timestamps)
        rel_timestamps = [(t - start_time) for t in timestamps]
        
        plt.step(rel_timestamps, occupancy, where='post')
        plt.xlabel('Time (seconds)')
        plt.ylabel('Chairs Occupied')
        plt.title('Chair Occupancy Over Time')
        plt.grid(True, alpha=0.3)
        plt.ylim(bottom=0, top=status_info['max_chairs'] + 1)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'chair_occupancy.png'))
        plt.close()
    
    # Plot 4: Barber status timeline
    if barber_status:
        plt.figure(figsize=(12, 4))
        timestamps, statuses = zip(*barber_status)
        
        # Convert timestamps to relative time in seconds
        start_time = min(timestamps)
        rel_timestamps = [(t - start_time) for t in timestamps]
        
        # Convert status to numeric (0 for sleeping, 1 for working)
        status_values = [1 if status == "working" else 0 for status in statuses]
        
        # Create a step plot
        plt.step(rel_timestamps, status_values, where='post')
        plt.yticks([0, 1], ['Sleeping', 'Working'])
        plt.xlabel('Time (seconds)')
        plt.title('Barber Status Over Time')
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'barber_status.png'))
        plt.close()
    
    # Plot 5: Customer throughput (customers served over time)
    plt.figure(figsize=(10, 6))
    if customer_data['service_end']:
        service_end_times = sorted(list(customer_data['service_end'].values()))
        customers_completed = list(range(1, len(service_end_times) + 1))
        
        # Convert timestamps to relative time in seconds
        if service_end_times:
            start_time = min(service_end_times)
            rel_service_end_times = [(t - start_time) for t in service_end_times]
            
            plt.step(rel_service_end_times, customers_completed, where='post')
            plt.xlabel('Time (seconds)')
            plt.ylabel('Customers Served')
            plt.title('Customer Throughput')
            plt.grid(True, alpha=0.3)
            plt.tight_layout()
            plt.savefig(os.path.join(output_dir, 'customer_throughput.png'))
    plt.close()
    
    # Plot 6: Pie chart of customers served vs lost
    plt.figure(figsize=(8, 8))
    labels = ['Served', 'Lost']
    sizes = [status_info['customers_served'], status_info['customers_lost']]
    colors = ['#4CAF50', '#F44336']
    
    plt.pie(sizes, labels=labels, colors=colors, autopct='%1.1f%%', startangle=90)
    plt.axis('equal')
    plt.title('Customer Service Status')
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'customer_service_status.png'))
    plt.close()

def generate_performance_summary(status_info, customer_data, output_dir):
    """Generate a performance summary based on the parsed data."""
    wait_times = list(customer_data['wait_times'].values())
    service_times = list(customer_data['service_times'].values())
    
    summary = {
        'Total Customers': status_info['customers_served'] + status_info['customers_lost'],
        'Customers Served': status_info['customers_served'],
        'Customers Lost': status_info['customers_lost'],
        'Service Rate': f"{status_info['customers_served'] / (status_info['customers_served'] + status_info['customers_lost']) * 100:.2f}%",
        'Loss Rate': f"{status_info['customers_lost'] / (status_info['customers_served'] + status_info['customers_lost']) * 100:.2f}%",
        'Average Wait Time': f"{np.mean(wait_times):.2f} seconds" if wait_times else "N/A",
        'Max Wait Time': f"{np.max(wait_times):.2f} seconds" if wait_times else "N/A",
        'Min Wait Time': f"{np.min(wait_times):.2f} seconds" if wait_times else "N/A",
        'Average Service Time': f"{np.mean(service_times):.2f} seconds" if service_times else "N/A",
        'Max Service Time': f"{np.max(service_times):.2f} seconds" if service_times else "N/A",
        'Min Service Time': f"{np.min(service_times):.2f} seconds" if service_times else "N/A",
        'Max Chairs': status_info['max_chairs'],
        'Current Chairs Occupied': status_info['chairs_occupied']
    }
    
    # Calculate system utilization (percentage of time barber is working)
    # This would require more detailed analysis of barber status over time
    
    # Calculate system efficiency measures
    if wait_times and service_times:
        summary['Average Total Time'] = f"{np.mean(wait_times) + np.mean(service_times):.2f} seconds"
        
    # Create a summary file
    with open(os.path.join(output_dir, 'performance_summary.txt'), 'w') as f:
        f.write("# Sleeping Barber Performance Summary\n\n")
        f.write(f"Analysis Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        
        f.write("## System Status\n")
        f.write(f"Current Status: {status_info['status']}\n")
        f.write(f"Chairs: {status_info['chairs_occupied']}/{status_info['max_chairs']} occupied\n")
        f.write(f"Customers Remaining: {status_info['customers_remaining']}\n\n")
        
        f.write("## Performance Metrics\n")
        f.write(f"Total Customers: {summary['Total Customers']}\n")
        f.write(f"Customers Served: {summary['Customers Served']} ({summary['Service Rate']})\n")
        f.write(f"Customers Lost: {summary['Customers Lost']} ({summary['Loss Rate']})\n\n")
        
        f.write("## Wait Time Metrics\n")
        f.write(f"Average Wait Time: {summary['Average Wait Time']}\n")
        f.write(f"Maximum Wait Time: {summary['Max Wait Time']}\n")
        f.write(f"Minimum Wait Time: {summary['Min Wait Time']}\n\n")
        
        f.write("## Service Time Metrics\n")
        f.write(f"Average Service Time: {summary['Average Service Time']}\n")
        f.write(f"Maximum Service Time: {summary['Max Service Time']}\n")
        f.write(f"Minimum Service Time: {summary['Min Service Time']}\n")
        
        if 'Average Total Time' in summary:
            f.write(f"\nAverage Total Time in System: {summary['Average Total Time']}\n")
    
    return summary

def main():
    parser = argparse.ArgumentParser(description='Analyze performance of the sleeping barber kernel module')
    parser.add_argument('--log', required=True, help='Path to the log file from the sleeping barber module')
    parser.add_argument('--output', default='barber_analysis', help='Output directory for performance reports')
    args = parser.parse_args()
    
    # Parse the log file
    print(f"Parsing log file: {args.log}")
    status_info, events = parse_barber_log(args.log)
    
    # Extract customer data
    print("Extracting customer data")
    customer_data = extract_customer_data(events)
    
    # Track chair occupancy
    print("Tracking chair occupancy")
    occupancy_data = track_chairs_occupancy(events)
    
    # Track barber status
    print("Tracking barber status")
    barber_status = track_barber_status(events)
    
    # Generate performance graphs
    print("Generating performance graphs")
    generate_performance_graphs(status_info, customer_data, occupancy_data, barber_status, args.output)
    
    # Generate performance summary
    print("Generating performance summary")
    summary = generate_performance_summary(status_info, customer_data, args.output)
    
    print(f"\nAnalysis complete! Results saved to: {args.output}")
    print("\nPerformance Summary:")
    print(f"Total Customers: {summary['Total Customers']}")
    print(f"Customers Served: {summary['Customers Served']} ({summary['Service Rate']})")
    print(f"Customers Lost: {summary['Customers Lost']} ({summary['Loss Rate']})")
    print(f"Average Wait Time: {summary['Average Wait Time']}")
    print(f"Average Service Time: {summary['Average Service Time']}")
    if 'Average Total Time' in summary:
        print(f"Average Total Time in System: {summary['Average Total Time']}")

if __name__ == "__main__":
    main()
