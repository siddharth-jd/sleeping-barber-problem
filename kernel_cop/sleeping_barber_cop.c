#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/time.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("Kernel Implementation of Sleeping Barber Problem");

#define PROC_ENTRY_NAME "sleeping_barber_cop"
#define DEFAULT_MAX_CHAIRS 10
#define MAX_LOG_ENTRIES 500
#define MAX_LOG_SIZE 16384
#define DEFAULT_NUM_CUSTOMERS 50

// Synchronization primitives
static struct semaphore mutex_sem;    // For accessing shared resources
static struct semaphore log_mutex;    // For accessing log

// Wait queues
static DECLARE_WAIT_QUEUE_HEAD(barber_wq);

// State variables
static int chairs_occupied = 0;
static int max_chairs = DEFAULT_MAX_CHAIRS;
static int total_customers_served = 0;
static int total_customers_lost = 0;
static int barber_sleeping = 1;
static int simulation_running = 0;
static int num_customers = DEFAULT_NUM_CUSTOMERS;
static int customers_remaining = 0;
static atomic_t customer_waiting = ATOMIC_INIT(0); // Flag for customer waiting

// Customer fairness implementation
struct customer {
    int id;
    unsigned long arrival_time;
    struct list_head list;
};

static LIST_HEAD(waiting_customers);

// Logging
struct log_entry {
    char message[128];
    unsigned long timestamp;
};

static struct log_entry *log_buffer;
static int log_index = 0;

// Thread handles
static struct task_struct *barber_thread;
static struct task_struct *customer_gen_thread;

// Add entry to the circular log buffer
static void add_log_entry(const char *fmt, ...) {
    va_list args;
    struct log_entry *entry;
    
    down(&log_mutex);
    
    if (log_buffer) {  // Check if log_buffer is initialized
        entry = &log_buffer[log_index];
        log_index = (log_index + 1) % MAX_LOG_ENTRIES;
        
        va_start(args, fmt);
        vsnprintf(entry->message, sizeof(entry->message), fmt, args);
        va_end(args);
        
        entry->timestamp = jiffies;
    }
    
    up(&log_mutex);
}

// Clear the log buffer
static void clear_log_buffer(void) {
    down(&log_mutex);
    if (log_buffer) {
        memset(log_buffer, 0, sizeof(struct log_entry) * MAX_LOG_ENTRIES);
        log_index = 0;
    }
    up(&log_mutex);
}

// Get a safe random interval for a delay
static unsigned int get_safe_random_interval(unsigned int min_ms, unsigned int max_ms) {
    unsigned int random_val = 0;
    unsigned int range = max_ms - min_ms + 1;
    
    get_random_bytes(&random_val, sizeof(random_val));
    return min_ms + (random_val % range);
}

// Barber function - completely rewritten for reliability
static int barber_function(void *data) {
    struct customer *next_customer;
    unsigned int cutting_time;
    int haircuts_done = 0;
    
    add_log_entry("Barber thread started");
    
    while (!kthread_should_stop()) {
        // Check for customers
        down(&mutex_sem);
        
        if (list_empty(&waiting_customers)) {
            // No customers, go to sleep
            barber_sleeping = 1;
            add_log_entry("Barber going to sleep (haircuts so far: %d)", haircuts_done);
            up(&mutex_sem);
            
            // Wait for customers or thread stop
            wait_event_interruptible(barber_wq, 
                atomic_read(&customer_waiting) || kthread_should_stop());
                
            if (kthread_should_stop()) {
                add_log_entry("Barber thread stopping while sleeping");
                return 0;
            }
            
            down(&mutex_sem);
            barber_sleeping = 0;
            add_log_entry("Barber woke up");
            
            // If list is empty after waking up (race condition)
            if (list_empty(&waiting_customers)) {
                add_log_entry("Barber woke up but found no customers, going back to sleep");
                up(&mutex_sem);
                continue;
            }
        }
        
        // Get the next customer
        if (!list_empty(&waiting_customers)) {
            next_customer = list_first_entry(&waiting_customers, struct customer, list);
            list_del(&next_customer->list);
            chairs_occupied--;
            
            add_log_entry("Barber is cutting hair for customer %d (waited %lu ms)", 
                         next_customer->id, 
                         (jiffies - next_customer->arrival_time) * 1000 / HZ);
            
            up(&mutex_sem);
            
            // Reset the customer waiting flag if no more customers
            down(&mutex_sem);
            if (list_empty(&waiting_customers)) {
                atomic_set(&customer_waiting, 0);
            }
            up(&mutex_sem);
            
            // Simulate haircut - use safe random interval
            cutting_time = get_safe_random_interval(100, 300);
            add_log_entry("Cutting hair for customer %d will take %u ms", next_customer->id, cutting_time);
            msleep(cutting_time);
            
            add_log_entry("Finished haircut for customer %d", next_customer->id);
            kfree(next_customer);
            
            haircuts_done++;
            total_customers_served++;
        } else {
            up(&mutex_sem);
        }
        
        // Small yield to prevent CPU hogging
        schedule();
    }
    
    add_log_entry("Barber thread exiting. Total haircuts: %d", haircuts_done);
    return 0;
}

// Customer generation function
static int customer_gen_function(void *data) {
    int customer_id = 1;
    unsigned int arrival_interval;
    struct customer *new_customer;
    
    add_log_entry("Customer generator started, will generate %d customers", num_customers);
    customers_remaining = num_customers;
    
    while (!kthread_should_stop() && customers_remaining > 0 && simulation_running) {
        // Create new customer
        new_customer = kmalloc(sizeof(struct customer), GFP_KERNEL);
        if (!new_customer) {
            add_log_entry("Failed to allocate memory for customer");
            msleep(100);
            continue;
        }
        
        new_customer->id = customer_id++;
        new_customer->arrival_time = jiffies;
        
        down(&mutex_sem);
        if (chairs_occupied < max_chairs) {
            // Add customer to waiting list
            list_add_tail(&new_customer->list, &waiting_customers);
            chairs_occupied++;
            
            add_log_entry("Customer %d arrived, waiting. Chairs occupied: %d/%d", 
                         new_customer->id, chairs_occupied, max_chairs);
            
            // Signal that a customer is waiting
            atomic_set(&customer_waiting, 1);
            
            // Wake up barber if sleeping
            if (barber_sleeping) {
                add_log_entry("Waking up barber for customer %d", new_customer->id);
                up(&mutex_sem);
                wake_up_interruptible(&barber_wq);
            } else {
                up(&mutex_sem);
            }
        } else {
            // Shop is full, customer leaves
            up(&mutex_sem);
            add_log_entry("Customer %d arrived but left (shop full)", new_customer->id);
            total_customers_lost++;
            kfree(new_customer);
        }
        
        customers_remaining--;
        
        // Use safe random interval for customer arrival
        arrival_interval = get_safe_random_interval(200, 400);
        add_log_entry("Next customer will arrive in %u ms", arrival_interval);
        msleep(arrival_interval);
    }
    
    add_log_entry("Customer generator finished - %s", 
                 customers_remaining == 0 ? "all customers generated" : "stopped early");
    return 0;
}

// Add a single customer (on demand)
static int add_single_customer(void) {
    static int manual_customer_id = 1000; // Start IDs for manually added customers from 1000
    struct customer *new_customer;
    
    if (!simulation_running) {
        add_log_entry("Cannot add customer: simulation not running");
        return -EINVAL;
    }
    
    new_customer = kmalloc(sizeof(struct customer), GFP_KERNEL);
    if (!new_customer) {
        add_log_entry("Failed to allocate memory for manual customer");
        return -ENOMEM;
    }
    
    new_customer->id = manual_customer_id++;
    new_customer->arrival_time = jiffies;
    
    down(&mutex_sem);
    
    if (chairs_occupied < max_chairs) {
        // Add customer to waiting list
        list_add_tail(&new_customer->list, &waiting_customers);
        chairs_occupied++;
        
        add_log_entry("Manual customer %d arrived, waiting. Chairs occupied: %d/%d", 
                     new_customer->id, chairs_occupied, max_chairs);
        
        // Signal that a customer is waiting
        atomic_set(&customer_waiting, 1);
        
        // Wake up barber if sleeping
        if (barber_sleeping) {
            add_log_entry("Waking up barber for manual customer %d", new_customer->id);
            up(&mutex_sem);
            wake_up_interruptible(&barber_wq);
        } else {
            up(&mutex_sem);
        }
        
        return 0;
    } else {
        // Shop is full, customer leaves
        up(&mutex_sem);
        add_log_entry("Manual customer %d arrived but left (shop full)", new_customer->id);
        total_customers_lost++;
        kfree(new_customer);
        return -EBUSY;
    }
}

// Control file operations
static ssize_t barber_read(struct file *file, char __user *buffer, size_t count, loff_t *offset) {
    char *kbuffer;
    int len = 0;
    int i, start;
    ssize_t ret;
    
    if (*offset > 0) return 0;
    
    kbuffer = kmalloc(MAX_LOG_SIZE, GFP_KERNEL);
    if (!kbuffer) return -ENOMEM;
    
    down(&log_mutex);
    
    // Add status information
    len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, 
                   "Status: %s\n", simulation_running ? "Running" : "Stopped");
    len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, 
                   "Chairs occupied: %d/%d\n", chairs_occupied, max_chairs);
    len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, 
                   "Customers served: %d\n", total_customers_served);
    len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, 
                   "Customers lost: %d\n", total_customers_lost);
    len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, 
                   "Customers remaining: %d\n\n", customers_remaining);
    
    // Add log entries, starting from oldest
    len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, "Event Log:\n");
    
    if (log_buffer) {
        if (log_index == 0) {
            start = 0;
        } else {
            start = log_index;
        }
        
        for (i = 0; i < MAX_LOG_ENTRIES; i++) {
            int idx = (start + i) % MAX_LOG_ENTRIES;
            if (log_buffer[idx].timestamp != 0) {
                len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, "[%5lu] %s\n", 
                               log_buffer[idx].timestamp / HZ, log_buffer[idx].message);
            }
        }
    } else {
        len += snprintf(kbuffer + len, MAX_LOG_SIZE - len, "Log not initialized\n");
    }
    
    up(&log_mutex);
    
    if (len > count) len = count;
    
    ret = copy_to_user(buffer, kbuffer, len);
    *offset += len - ret;
    
    kfree(kbuffer);
    return len - ret;
}

static ssize_t barber_write(struct file *file, const char __user *buffer, size_t count, loff_t *offset) {
    char command[64];
    size_t command_len = min(count, sizeof(command) - 1);
    int value;
    
    if (copy_from_user(command, buffer, command_len)) {
        return -EFAULT;
    }
    command[command_len] = '\0';
    
    // Process commands
    if (strncmp(command, "start", 5) == 0) {
        int num_chairs = DEFAULT_MAX_CHAIRS;
        
        // Parse number of customers and chairs if provided
        if (sscanf(command, "start %d %d", &value, &num_chairs) == 2 && value > 0 && num_chairs > 0) {
            num_customers = value;
            max_chairs = num_chairs;
        } else if (sscanf(command, "start %d", &value) == 1 && value > 0) {
            num_customers = value;
            // Keep default chairs
        } else {
            num_customers = DEFAULT_NUM_CUSTOMERS;  // Default
        }
        
        if (!simulation_running) {
            simulation_running = 1;
            
            // Initialize log buffer if needed
            if (!log_buffer) {
                log_buffer = kmalloc(sizeof(struct log_entry) * MAX_LOG_ENTRIES, GFP_KERNEL);
                if (!log_buffer) return -ENOMEM;
                clear_log_buffer();
            }
            
            // Reset counters and state
            down(&mutex_sem);
            chairs_occupied = 0;
            total_customers_served = 0;
            total_customers_lost = 0;
            barber_sleeping = 1;
            atomic_set(&customer_waiting, 0);
            
            // Clear any remaining customers
            while (!list_empty(&waiting_customers)) {
                struct customer *cust = list_first_entry(&waiting_customers, struct customer, list);
                list_del(&cust->list);
                kfree(cust);
            }
            up(&mutex_sem);
            
            // Create barber thread first
            barber_thread = kthread_run(barber_function, NULL, "barber_thread");
            if (IS_ERR(barber_thread)) {
                add_log_entry("Failed to start barber thread");
                simulation_running = 0;
                return -EAGAIN;
            }
            
            // Small delay to ensure barber is initialized
            msleep(50);
            
            // Create customer generator thread
            customer_gen_thread = kthread_run(customer_gen_function, NULL, "customer_gen_thread");
            if (IS_ERR(customer_gen_thread)) {
                add_log_entry("Failed to start customer generator thread");
                kthread_stop(barber_thread);
                simulation_running = 0;
                return -EAGAIN;
            }
            
            add_log_entry("Simulation started with %d chairs and %d customers", max_chairs, num_customers);
        } else {
            add_log_entry("Simulation already running");
        }
    } else if (strncmp(command, "stop", 4) == 0) {
        if (simulation_running) {
            simulation_running = 0;
            
            // Stop threads safely
            if (customer_gen_thread && !IS_ERR(customer_gen_thread)) {
                add_log_entry("Stopping customer generator thread");
                kthread_stop(customer_gen_thread);
                customer_gen_thread = NULL;
            }
            
            // Wake up barber if sleeping, then stop
            atomic_set(&customer_waiting, 1);
            wake_up_interruptible(&barber_wq);
            
            if (barber_thread && !IS_ERR(barber_thread)) {
                add_log_entry("Stopping barber thread");
                kthread_stop(barber_thread);
                barber_thread = NULL;
            }
            
            add_log_entry("Simulation stopped");
        } else {
            add_log_entry("Simulation not running");
        }
    } else if (strncmp(command, "reset", 5) == 0) {
        // First stop if running
        if (simulation_running) {
            simulation_running = 0;
            
            // Stop threads safely
            if (customer_gen_thread && !IS_ERR(customer_gen_thread)) {
                kthread_stop(customer_gen_thread);
                customer_gen_thread = NULL;
            }
            
            // Wake up barber if sleeping, then stop
            atomic_set(&customer_waiting, 1);
            wake_up_interruptible(&barber_wq);
            
            if (barber_thread && !IS_ERR(barber_thread)) {
                kthread_stop(barber_thread);
                barber_thread = NULL;
            }
        }
        
        // Reset all counters and state
        down(&mutex_sem);
        chairs_occupied = 0;
        total_customers_served = 0;
        total_customers_lost = 0;
        barber_sleeping = 1;
        num_customers = DEFAULT_NUM_CUSTOMERS;
        customers_remaining = 0;
        atomic_set(&customer_waiting, 0);
        
        // Clear any remaining customers
        while (!list_empty(&waiting_customers)) {
            struct customer *cust = list_first_entry(&waiting_customers, struct customer, list);
            list_del(&cust->list);
            kfree(cust);
        }
        up(&mutex_sem);
        
        clear_log_buffer();
        add_log_entry("Simulation reset");
    } else if (strncmp(command, "set_chairs", 10) == 0) {
        if (sscanf(command, "set_chairs %d", &value) == 1 && value > 0) {
            max_chairs = value;
            add_log_entry("Max chairs set to %d", max_chairs);
        } else {
            add_log_entry("Invalid chairs value");
        }
    } else if (strncmp(command, "add_customer", 12) == 0) {
        int result = add_single_customer();
        if (result != 0) {
            add_log_entry("Failed to add customer: %d", result);
        }
    } else {
        add_log_entry("Unknown command: %s", command);
    }
    
    return count;
}

static struct proc_ops barber_proc_ops = {
    .proc_read = barber_read,
    .proc_write = barber_write,
};

static struct proc_dir_entry *barber_proc_entry;

static int __init barber_init(void) {
    // Initialize synchronization primitives
    sema_init(&mutex_sem, 1);
    sema_init(&log_mutex, 1);
    
    // Create proc file entry
    barber_proc_entry = proc_create(PROC_ENTRY_NAME, 0666, NULL, &barber_proc_ops);
    if (!barber_proc_entry) {
        printk(KERN_ERR "Sleeping Barber: Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    printk(KERN_INFO "Sleeping Barber: Module loaded\n");
    return 0;
}

static void __exit barber_exit(void) {
    // Stop any running threads
    if (simulation_running) {
        simulation_running = 0;
        
        if (customer_gen_thread && !IS_ERR(customer_gen_thread)) {
            kthread_stop(customer_gen_thread);
        }
        
        // Wake up barber if sleeping before stopping
        atomic_set(&customer_waiting, 1);
        wake_up_interruptible(&barber_wq);
        
        if (barber_thread && !IS_ERR(barber_thread)) {
            kthread_stop(barber_thread);
        }
    }
    
    // Free memory
    if (log_buffer) {
        kfree(log_buffer);
        log_buffer = NULL;
    }
    
    // Clean up any remaining customers
    down(&mutex_sem);
    while (!list_empty(&waiting_customers)) {
        struct customer *cust = list_first_entry(&waiting_customers, struct customer, list);
        list_del(&cust->list);
        kfree(cust);
    }
    up(&mutex_sem);
    
    // Remove proc entry
    proc_remove(barber_proc_entry);
    
    printk(KERN_INFO "Sleeping Barber: Module unloaded\n");
}

module_init(barber_init);
module_exit(barber_exit);
