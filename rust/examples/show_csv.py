import matplotlib.pyplot as plt
import csv

CSV_FILENAME = 'curves.csv'

# Function to read CSV file and return data
def read_csv(filename):
    curves = []
    with open(filename, 'r') as csvfile:
        csvreader = csv.reader(csvfile)
        for row in csvreader:
            curves.append([float(val) for val in row])
    return list(zip(*curves))  # Transpose to get columns as separate lists

# Read data from CSV
curves = read_csv(CSV_FILENAME)

# Plot the data
plt.figure(figsize=(10, 6))
for curve in curves:
    plt.plot(range(len(curve)), curve)

plt.title('Curves Plot')
plt.xlabel('Index')
plt.ylabel('Value')
plt.grid(True)
plt.legend(['Curve 1', 'Curve 2', 'Curve 3'])

plt.show()
