import socket
import threading
import sys

SERVER_IP = "127.0.0.1"
SERVER_PORT = 8888
BUFFER_SIZE = 1024

def receive_messages(client_socket):
    while True:
        try:
            message = client_socket.recv(BUFFER_SIZE).decode()
            print(message)
        except:
            print("An error occurred!")
            client_socket.close()
            break

def main():
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect((SERVER_IP, SERVER_PORT))

    username = input("Enter your username: ")
    client_socket.send(f"LOGIN {username}".encode())

    threading.Thread(target=receive_messages, args=(client_socket,)).start()

    while True:
        message = input()
        if message.lower() == "logout":
            client_socket.send(f"LOGOUT {username}".encode())
            break
        elif message.startswith("file"):
            # Handle file transfer
            pass
        else:
            client_socket.send(f"MSG {username} {message}".encode())

    client_socket.close()

if __name__ == "__main__":
    main()
