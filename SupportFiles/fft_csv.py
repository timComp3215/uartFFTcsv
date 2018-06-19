'''
This program reads SAMPLES no. of inputs from fft_input.csv
and sends to COM4 at 9600 baud rate.
It then receives back the magnitudes from the FFT
The output is stored in fft_output.csv
The input and output are then displayed using matplotlib
'''

#Import libraries
import serial
import csv
import matplotlib.pyplot as plt

#Set length of signal
SAMPLES = 1024

#Sampling frequency - purely for graphs to be correctly scaled
fs = 8192

input_file = 'fft_input.csv'
output_file = 'fft_output.csv'

print "Send input signal of length " + str(fs) + " from " + input_file + " to board and receive FFT magnitude"
print "Store results in " + output_file + " and display input and output using Matplotlib"

#Store input values
values = []

#Read inputs from csv
with open(input_file, 'rb') as f:
    reader = csv.reader(f)
    for row in reader:
        values.append(row)

    f.close()

#print values

#Connect to serial channel and send input
s = serial.Serial('COM4', 9600)

print "Sending values to board..."

#Write
for x in range(SAMPLES):
    num = int(values[x][0])

    #Turn into signed number
    if (num <0):
        num = num + 2**16

    #Split into bytes
    s.write(chr(num % 256))
    s.write(chr(num / 256))
    
    #print 'Message sent ' + str(num)

print "Reading messages from board.."

#Read messages
magnitude = []
    
n=0
for n in range(SAMPLES/2):

    msg = s.read(2)

    #Turn signed 16 bit number back into python integer
    receivedMsg = int(msg[0].encode('hex'), 16) + 256*int(msg[1].encode('hex'), 16)

    #Signed -> Normal
    if (receivedMsg > (2**15)-1):
        receivedMsg = receivedMsg - (2**16)

    #print str(receivedMsg)

    magnitude.append(receivedMsg)

#Close serial channel
s.close()

#Store output in csv
print "Write output to fft_output.csv"
with open(output_file, 'wb') as fout:
    writer = csv.writer(fout)
    for n in range(0, SAMPLES/2):
        #Integer must be inside [] brackets
        writer.writerow([magnitude[n]])

    fout.close()

#Set up x axes variables for plotting
time = []

for n in range(0, SAMPLES):
    time.append(n*(1/float(fs)))

fbins = []

for n in range(0, SAMPLES/2):
    fbins.append(n*(fs/SAMPLES))

#Plot as 2 subolots
fig = plt.figure(1)

#Input signal
plt.subplot(211)
plt.plot(time, values, linewidth=0.5)
plt.ylabel('Acceleration (ms-2)')
plt.xlabel('Time (s)')
plt.title('Time based data')
#Get rid of white space on x axis
plt.autoscale(enable=True, axis='x', tight=True)
#Put ticks on outside for bottom and left axes
ax = plt.gca()
ax.tick_params(direction='out')
ax.tick_params(bottom=True, left=True, top=False, right=False)

#FFT spectrum
plt.subplot(212)
plt.plot(fbins, magnitude, linewidth=0.5)
plt.ylabel('Magnitude')
plt.xlabel('Frequency (Hz)')
plt.title('Frequency spectrum')
plt.autoscale(enable=True, axis='x', tight=True)
ax = plt.gca()
ax.tick_params(direction='out')
ax.tick_params(bottom=True, left=True, top=False, right=False)

#Background colour
fig.patch.set_facecolor('white')

#Space subplots properly
plt.tight_layout()

#Display
print "Display graphs"
print "Please close graphs before continuing"
plt.show()

