import subprocess
import sys
import threading
import sounddevice as sd
import os
import numpy as np
import time
import queue  
import speech_recognition as sr
import cv2  
import json
import base64
import pywhatkit
from email.message import EmailMessage
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from groq import Groq
from datetime import datetime, timezone, timedelta
GROQ_API_KEY=""
client = Groq(api_key=GROQ_API_KEY)
MODEL_NAME = "qwen/qwen3.6-27b"

# Added personalization to the System Prompt
SYSTEM_PROMPT = (
    "You are an articulate, engaging, and conversational AI assistant running on a pair of smart glasses. "
    "Your responses are spoken aloud directly into the ear of your user, Anhad. "
    "Speak naturally and expressively, like a knowledgeable friend. "
    "Stay focused on the user's intent and provide only information relevant to fulfilling it. "
    "Keep responses concise by default, and provide additional detail only when it is useful or requested. "
    "VISION RULE: When asked about what you see, hyper-focus ONLY on the primary subject relevant to the user's request. "
    "Do not describe the background, room, user's body, lighting, or unrelated objects unless explicitly asked. "
    "UNCERTAINTY RULE: Never invent, assume, or hallucinate visual details. "
    "If the subject is unclear, obscured, distant, or unreadable, say so rather than guessing. "
    "Do not use filler phrases like 'I see'. Do not use markdown, emojis, asterisks, or special formatting. "
    "for time based outputs don't say 7 o clock pm, instead say 7 p.m. or 7 in the enevening. For dates, say 'August 16th' instead of 'August 16'. "
    "When scheduling events, if the user does not specify a duration, DO NOT create the event. "
    "Instead, ask the user: 'How long should this session be?' and wait for their answer before creating the event. "
    "Only call the create_event tool once you have both a start time and a duration."

    "When using the create_event tool, you MUST provide the FULL ISO 8601 string including the year, month, day, 'T', hours, minutes, seconds, and the timezone offset (+05:30). "
    "NEVER truncate dates (e.g., '2026-'). If you are unsure of the full timestamp, ask the user to confirm the date and time again."
    "WHENEVER THE USER ASKS YOU TO SCHEDULE AN EVENT, OR BOOK A SLOT, OR SET A REMINDER, OR CREATE A TASK, OR ANYTHING RELATED, YOU MUST USE THE create_event TOOL. DO NOT SAY YOU HAVE SCHEDULED SOMETHING UNLESS YOU HAVE ACTIVELY CALLED THE TOOL."
)

# Groq Tool Schema
TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "get_unread_emails",
            "description": "Fetch the user's latest unread emails from their Gmail inbox.",
            "parameters": {
                "type": "object",
                "properties": {
                    "max_results": {"type": "integer", "description": "Number of emails to fetch. Default is 3."}
                }
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "send_email",
            "description": "Send an email to a specific email address.",
            "parameters": {
                "type": "object",
                "properties": {
                    "to_address": {"type": "string", "description": "The recipient's email address."},
                    "subject": {"type": "string", "description": "The subject line of the email."},
                    "body": {"type": "string", "description": "The main content of the email."}
                },
                "required": ["to_address", "subject", "body"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "get_upcoming_events",
            "description": "Fetch upcoming schedule, meetings, and events from the user's Google Calendar.",
            "parameters": {
                "type": "object",
                "properties": {
                    "max_results": {"type": "integer", "description": "Number of events to retrieve. Default is 5."}
                }
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "create_event",
            "description": "Create a new event on the user's Google Calendar. start_time and end_time MUST be ISO 8601 strings with timezone offset (e.g., '2026-08-16T18:30:00+05:30').",
            "parameters": {
                "type": "object",
                "properties": {
                    "summary": {"type": "string", "description": "Title of the event."},
                    "start_time": {"type": "string", "description": "Start date and time in ISO format with timezone offset."},
                    "end_time": {"type": "string", "description": "End date and time in ISO format (optional)."},
                    "description": {"type": "string", "description": "Notes or details (optional)."}
                },
                "required": ["summary", "start_time"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "get_news",
            "description": "Fetch top news headlines for a specific category and country.",
            "parameters": {
                "type": "object",
                "properties": {
                    "category": {"type": "string", "description": "Category like 'business', 'technology', 'sports', 'general'."},
                    "country": {"type": "string", "description": "Two-letter country code. Use 'in' for India, 'us' for USA."}
                }
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "search_drive",
            "description": "Search the user's Google Drive for a document or text file by name and read its contents.",
            "parameters": {
                "type": "object",
                "properties": {
                    "query": {
                        "type": "string", 
                        "description": "The name of the file or document to search for. Keep it concise."
                    }
                },
                "required": ["query"]
            }
        }
    }
]

# --- Vision debugging ---
DEBUG_SAVE_CAPTURES = True
DEBUG_AUTO_OPEN = True
DEBUG_DIR = os.path.expanduser("~/vision_debug_captures")

SCOPES = [
    'https://www.googleapis.com/auth/gmail.modify',
    'https://www.googleapis.com/auth/calendar',
    'https://www.googleapis.com/auth/drive.readonly'
]

from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
import io

class DriveIntegration:
    def __init__(self, creds):
        self.service = build('drive', 'v3', credentials=creds)
        print("[System]: Google Drive API Authenticated Successfully.")

    def search_and_read_file(self, query):
        """Searches for a Google Doc or text file by name and reads its content."""
        try:
            # 1. Search for the file by name
            # We use 'contains' to make the search flexible
            search_query = f"name contains '{query}'"
            results = self.service.files().list(
                q=search_query, 
                pageSize=1, # Grab the best match
                fields="files(id, name, mimeType)"
            ).execute()
            
            items = results.get('files', [])
            
            if not items:
                return f"I couldn't find any files matching '{query}' in your Google Drive."
                
            file_id = items[0]['id']
            file_name = items[0]['name']
            mime_type = items[0]['mimeType']
            
            print(f"[System]: Found file '{file_name}'. Extracting text...")

            # 2. Extract the content based on file type
            if mime_type == 'application/vnd.google-apps.document':
                # It's a Google Doc, export it as plain text
                request = self.service.files().export_media(fileId=file_id, mimeType='text/plain')
            elif mime_type == 'text/plain':
                # It's a standard text file
                request = self.service.files().get_media(fileId=file_id)
            else:
                return f"The file '{file_name}' was found, but it is a {mime_type} which I cannot read aloud."

            # Read the byte stream
            file_content = request.execute().decode('utf-8')
            
            # Truncate content to avoid token limits for the LLM
            max_chars = 3000 
            if len(file_content) > max_chars:
                file_content = file_content[:max_chars] + "\n...[Content truncated for length]"
                
            return f"Here is the content of the file '{file_name}':\n\n{file_content}"

        except HttpError as error:
            print(f"\n[Drive Error]: {error}")
            return f"An error occurred while trying to access Google Drive: {error}"

from newsapi import NewsApiClient

class NewsIntegration:
    def __init__(self, api_key):
        self.newsapi = NewsApiClient(api_key=api_key)
        print("[System]: News API Authenticated Successfully.")

    def get_news(self, category="general", country="in"):
        """Fetches top headlines with robust error handling."""
        try:
            # Remove country='in' to see if it's just a local index issue
            top_headlines = self.newsapi.get_top_headlines(
                category=category,
                language='en'
            )
            
            articles = top_headlines.get('articles', [])
            if not articles:
                return "No tech headlines found right now."
            
            # Initialize the variable explicitly
            news_summary = "Here are the top headlines: "
            
            # Use enumerate to build the string
            headlines = []
            for i, article in enumerate(articles[:3]):
                title = article.get('title', 'Untitled')
                headlines.append(f"{i+1}. {title}")
            
            return news_summary + ". ".join(headlines) + "."
            
        except Exception as e:
            print(f"\n[News Error]: {e}")
            return f"Error fetching news: {e}"

class GmailIntegration:
    def __init__(self):
        self.creds = None
        if os.path.exists('token.json'):
            self.creds = Credentials.from_authorized_user_file('token.json', SCOPES)
        
        if not self.creds or not self.creds.valid:
            if self.creds and self.creds.expired and self.creds.refresh_token:
                self.creds.refresh(Request())
            else:
                flow = InstalledAppFlow.from_client_secrets_file('credentials.json', SCOPES)
                self.creds = flow.run_local_server(port=0)
            with open('token.json', 'w') as token:
                token.write(self.creds.to_json())
                
        self.service = build('gmail', 'v1', credentials=self.creds)
        print("[System]: Gmail API Authenticated Successfully.")

    def get_unread_emails(self, max_results=3):
        try:
            results = self.service.users().messages().list(userId='me', labelIds=['INBOX', 'UNREAD'], maxResults=max_results).execute()
            messages = results.get('messages', [])
            if not messages:
                return "You have no unread emails."
            email_summaries = []
            for msg in messages:
                txt = self.service.users().messages().get(userId='me', id=msg['id'], format='metadata', metadataHeaders=['From', 'Subject']).execute()
                headers = txt['payload']['headers']
                sender = next((header['value'] for header in headers if header['name'] == 'From'), "Unknown")
                subject = next((header['value'] for header in headers if header['name'] == 'Subject'), "No Subject")
                email_summaries.append(f"From: {sender}, Subject: {subject}")
            return "\n".join(email_summaries)
        except Exception as e:
            return f"Error fetching emails: {e}"

    def send_email(self, to_address, subject, body):
        try:
            message = EmailMessage()
            message.set_content(body)
            message['To'] = to_address
            message['From'] = 'me'
            message['Subject'] = subject
            encoded_message = base64.urlsafe_b64encode(message.as_bytes()).decode()
            create_message = {'raw': encoded_message}
            self.service.users().messages().send(userId="me", body=create_message).execute()
            return f"Email successfully sent to {to_address}."
        except Exception as e:
            return f"Failed to send email: {e}"

class CalendarIntegration:
    def __init__(self, creds):
        self.service = build('calendar', 'v3', credentials=creds)
        print("[System]: Google Calendar API Authenticated Successfully.")

    def get_upcoming_events(self, max_results=10):
        """Fetches the next upcoming events from the primary calendar."""
        try:
            now = datetime.now(timezone.utc).isoformat()
            events_result = self.service.events().list(
                calendarId='primary',
                timeMin=now,
                maxResults=max_results,
                singleEvents=True,
                orderBy='startTime'
            ).execute()
            
            events = events_result.get('items', [])

            if not events:
                return "You have no upcoming events on your calendar."

            event_list = []
            for event in events:
                # Extract both start and end times
                start = event['start'].get('dateTime', event['start'].get('date'))
                end = event['end'].get('dateTime', event['end'].get('date'))
                summary = event.get('summary', 'Untitled Event')
                
                # Format the string to give Groq strict time boundaries
                event_list.append(f"- '{summary}' | Starts: {start} | Ends: {end}")

            return "Upcoming events:\n" + "\n".join(event_list)
        except Exception as e:
            print(f"\n[Calendar Error]: {e}")
            return f"Error fetching calendar events: {e}"

    def create_event(self, summary, start_time, end_time=None, description=""):
        """Creates an event on the user's primary calendar."""
        try:
            # Prevent Google from rejecting 0-minute events by defaulting to a 1-hour block
            if not end_time:
                start_dt = datetime.fromisoformat(start_time)
                end_time = (start_dt + timedelta(hours=1)).isoformat()

            event = {
                'summary': summary,
                'description': description,
                'start': {'dateTime': start_time},
                'end': {'dateTime': end_time},
            }
            self.service.events().insert(calendarId='primary', body=event).execute()
            return f"Event '{summary}' successfully created for {start_time}."
        except Exception as e:
            print(f"\n[Calendar Error]: {e}")
            return f"Failed to create event: {e}"

class AssistantApp:
    def __init__(self):
        self.is_awake = False
        self.vision_mode = False 
        
        # --- NEW: DYNAMIC TIME INJECTION ---
        # Grabs the live system time and year so the AI never guesses
        # Grabs the live system time, year, AND timezone offset
        current_context = datetime.now().astimezone().strftime("%A, %B %d, %Y %I:%M %p %z")
        
        dynamic_prompt = SYSTEM_PROMPT + f"\nCRITICAL DATA: Today is {current_context}. You MUST use the create_event tool to schedule calendar events. Never say you have scheduled something unless you have actively called the tool."
        self.chat_history = [{"role": "system", "content": dynamic_prompt}]
        
        self.tts_queue = queue.Queue()
        threading.Thread(target=self.tts_worker, daemon=True).start()
        
        # Initialize Gmail Client
        # Initialize Google Clients
        self.gmail_client = GmailIntegration()
        self.calendar_client = CalendarIntegration(self.gmail_client.creds)
        self.news_client = NewsIntegration(api_key="")
        self.drive_client = DriveIntegration(self.gmail_client.creds)
        
        # 1. INITIALIZE CLOUD CONNECTION
        print(f"[System]: Authenticating with Groq Cloud ({MODEL_NAME})...")
        try:
            # Quick ping test
            client.chat.completions.create(
                model=MODEL_NAME,
                messages=[{"role": "user", "content": "system check"}],
                max_tokens=5,
                reasoning_effort="none"
            )
            print("[System]: Groq Cloud Connection Established.")
        except Exception as e:
            print(f"[System]: API Connection Failed: {e}")
            sys.exit(1)

        print("[System]: Loading local STT Engine...")
        threading.Thread(target=self.wake_word_listener, daemon=True).start()
        threading.Thread(target=self.manual_trigger_listener, daemon=True).start()
        
    def tts_worker(self):
        while True:
            text = self.tts_queue.get()
            if text:
                subprocess.run(["say", "-r", "240", text])  
                # rajraman21211@gmail.com
            self.tts_queue.task_done()

    def manual_trigger_listener(self):
        while True:
            input()  
            if not self.is_awake:
                self.start_conversation()

    def start_conversation(self):
        self.is_awake = True
        subprocess.Popen(["afplay", "/System/Library/Sounds/Glass.aiff"])
        threading.Thread(target=self.conversation_loop).start()

    def capture_frame_base64(self):
        """Captures a frame and encodes it to base64 for Groq's data-uri payload."""
        print("[System]: Snapping high-res frame for Groq...")
        cap = cv2.VideoCapture(0)

        # Ask for a resolution way above what any webcam actually has — the
        # driver clamps this to the highest mode it really supports, which is
        # how you get the camera's true max instead of OpenCV's low default.
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 4032)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 3024)

        # The first frame off a freshly opened capture is usually grabbed
        # before autofocus/auto-exposure has converged, so it comes out soft —
        # this alone can be enough to make handwriting unreadable. Burn a few
        # frames to let the camera settle before taking the one we actually send.
        for _ in range(5):
            cap.read()
        ret, frame = cap.read()
        actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        cap.release()
        
        if ret:
            # Only downscale if it's bigger than needed for upload speed —
            # never upscale, since that just blurs detail without adding any.
            max_dim = 1920
            h, w = frame.shape[:2]
            if max(h, w) > max_dim:
                scale = max_dim / max(h, w)
                frame = cv2.resize(frame, (int(w * scale), int(h * scale)))

            _, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 90])

            if DEBUG_SAVE_CAPTURES:
                os.makedirs(DEBUG_DIR, exist_ok=True)
                debug_path = os.path.join(DEBUG_DIR, "last_capture.jpg")
                with open(debug_path, "wb") as f:
                    f.write(buffer)
                print(f"[Debug]: camera reported {actual_w}x{actual_h}, sending {frame.shape[1]}x{frame.shape[0]} -> {debug_path}")
                if DEBUG_AUTO_OPEN:
                    subprocess.Popen(["open", debug_path])

            return base64.b64encode(buffer).decode('utf-8')
            
        return None

    def wake_word_listener(self):
        print("[System]: Wake word active! Say 'Wake up' or press ENTER.")
        r = sr.Recognizer()
        samplerate = 16000
        chunk_size = 4000
        threshold = 500.0
        
        with sd.RawInputStream(samplerate=samplerate, blocksize=chunk_size, dtype='int16', channels=1) as stream:
            has_spoken = False
            silence_chunks = 0
            audio_frames = bytearray()
            
            while True:
                data, _ = stream.read(chunk_size)
                
                if self.is_awake:
                    has_spoken = False
                    silence_chunks = 0
                    audio_frames = bytearray()
                    continue
                    
                audio_data = np.frombuffer(data, dtype=np.int16)
                rms = np.sqrt(np.mean(np.square(audio_data.astype(np.float32))))
                
                if rms > threshold:
                    has_spoken = True
                    silence_chunks = 0
                elif has_spoken:
                    silence_chunks += 1
                
                if has_spoken:
                    audio_frames.extend(data)
                else:
                    audio_frames = bytearray(data)
                    
                if has_spoken and silence_chunks >= 3:
                    try:
                        audio = sr.AudioData(bytes(audio_frames), samplerate, 2)
                        text = r.recognize_google(audio).lower()
                        
                        if any(word in text for word in ["wake up", "wake", "up", "hi"]):
                            self.start_conversation()
                    except:
                        pass 
                        
                    has_spoken = False
                    silence_chunks = 0
                    audio_frames = bytearray()

    @staticmethod
    def _clean_for_tts(text):
        """Strip markdown noise plus any leading '-'/'*' left over from list-style
        formatting. Without this, a chunk starting with '-' gets parsed by macOS
        `say` as a command-line flag instead of text ('say: invalid option --')."""
        cleaned = text.strip().replace('*', '').replace('#', '').replace('"', '')
        return cleaned.lstrip('-* ').strip()

    def process_text_stream(self, question: str, base64_image: str = None):
        try:
            print("[Assistant]: ", end="", flush=True)
            
            if base64_image:
                self.chat_history = [self.chat_history[0]]
                user_content = [
                    {"type": "text", "text": question},
                    {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{base64_image}"}}
                ]
            else:
                user_content = question
            
            self.chat_history.append({"role": "user", "content": user_content})
            
            # Phase 1: Tool Checking (Only if no image is attached)
            if not base64_image:
                routing_response = client.chat.completions.create(
                    model=MODEL_NAME,
                    messages=self.chat_history,
                    tools=TOOLS,
                    tool_choice="auto",
                    max_tokens=500,
                )
                
                response_message = routing_response.choices[0].message
                tool_calls = response_message.tool_calls
                
                if tool_calls:
                    # Append the AI's intent to use a tool to history
                    self.chat_history.append(response_message)
                    
                    for tool_call in tool_calls:
                        function_name = tool_call.function.name
                        function_args = json.loads(tool_call.function.arguments)
                        
                        if function_name == "get_unread_emails":
                            print(f"\n[System]: Fetching Gmail data...")
                            result = self.gmail_client.get_unread_emails(max_results=function_args.get("max_results", 3))
                        elif function_name == "send_email":
                            print(f"\n[System]: Sending email to {function_args.get('to_address')}...")
                            result = self.gmail_client.send_email(
                                function_args.get("to_address"),
                                function_args.get("subject"),
                                function_args.get("body")
                            )
                        elif function_name == "get_upcoming_events":
                            print(f"\n[System]: Fetching Calendar events...")
                            result = self.calendar_client.get_upcoming_events(
                                max_results=function_args.get("max_results", 5)
                            )
                        elif function_name == "create_event":
                            summary = function_args.get("summary")
                            start_time = function_args.get("start_time")
                            end_time = function_args.get("end_time")
                            desc = function_args.get("description", "")
                        elif function_name == "get_news":
                            cat = function_args.get("category", "general")
                            cou = function_args.get("country", "in")
                            print(f"\n[System]: Fetching {cat} news for {cou}...")
                            
                            # Ensure this matches the method name exactly
                            result = self.news_client.get_news(category=cat, country=cou)
                        elif function_name == "search_drive":
                            search_query = function_args.get("query", "")
                            print(f"\n[System]: Searching Google Drive for '{search_query}'...")
                            result = self.drive_client.search_and_read_file(query=search_query)
                        
                        # Append the raw tool result back into the context
                        self.chat_history.append({
                            "role": "tool",
                            "tool_call_id": tool_call.id,
                            "name": function_name,
                            "content": str(result)
                        })

            # Phase 2: Streaming Output (Converts context or tool results into speech)
            response_stream = client.chat.completions.create(
                model=MODEL_NAME,
                messages=self.chat_history,
                stream=True,
                temperature=0.1,
                max_tokens=500,
                reasoning_effort="none"
            )
            
            full_response = ""
            sentence_buffer = ""
            trigger_chars = ['.', '!', '?', '\n']
            
            for chunk in response_stream:
                if chunk.choices[0].delta.content:
                    token = chunk.choices[0].delta.content
                    print(token, end="", flush=True)
                    
                    sentence_buffer += token
                    full_response += token
                    
                    if any(p in token for p in trigger_chars) and len(sentence_buffer.strip()) > 3:
                        clean_sentence = self._clean_for_tts(sentence_buffer)
                        if clean_sentence:
                            self.tts_queue.put(clean_sentence) 
                        sentence_buffer = ""
            
            if sentence_buffer.strip():
                clean_sentence = self._clean_for_tts(sentence_buffer)
                if clean_sentence:
                    self.tts_queue.put(clean_sentence)
            
            print()  
            self.chat_history.append({"role": "assistant", "content": full_response})
            
            # Prune history to prevent context window bloat
            if len(self.chat_history) > 8:
                self.chat_history = [self.chat_history[0]] + self.chat_history[-6:]
                
            self.tts_queue.join()
                
        except Exception as e:
            print(f"\n[System]: Groq Cloud Error: {e}")

    def record_and_transcribe_in_ram(self, is_vision_active):
        r = sr.Recognizer()
        try:
            samplerate = 16000
            chunk_size = 4000  
            threshold = 500.0 
            
            # Play the activation chime
            subprocess.Popen(["afplay", "/System/Library/Sounds/Tink.aiff"])
            
            # FIX 1: THE CHIME COOLDOWN
            # Sleep for exactly 300ms to let the physical chime echo fade in the room 
            # before we start analyzing microphone volume.
            time.sleep(0.3)
            
            with sd.RawInputStream(samplerate=samplerate, blocksize=chunk_size, channels=1, dtype='int16') as stream:
                
                if is_vision_active:
                    print(f"\n[System]: Listening... (VISION ACTIVE) -> [BLUE LED ON]")
                else:
                    print(f"\n[System]: Listening... -> [BLUE LED ON]")
                
                # Flush any stale audio from the buffer
                stream.read(chunk_size) 
                
                has_spoken = False
                consecutive_loud_chunks = 0
                idle_chunks = 0
                trailing_silence = 0
                audio_frames = bytearray()
                
                while True:
                    data, _ = stream.read(chunk_size)
                    audio_data = np.frombuffer(data, dtype=np.int16)
                    rms = np.sqrt(np.mean(np.square(audio_data.astype(np.float32))))
                    
                    if rms > threshold:
                        consecutive_loud_chunks += 1
                        idle_chunks = 0  # Reset the idle timer because we heard noise
                        
                        # FIX 2: THE SPEECH DEBOUNCE
                        # Must be loud for at least 2 consecutive chunks (~0.5s) to count as speech.
                        if consecutive_loud_chunks >= 2:
                            has_spoken = True
                            trailing_silence = 0
                    else:
                        consecutive_loud_chunks = 0
                        
                        # FIX 3: THE TRUE IDLE TIMER
                        if has_spoken:
                            trailing_silence += 1  # Counting silence AFTER you spoke
                        else:
                            idle_chunks += 1       # Counting silence BEFORE you spoke (fixes infinite hang)
                            
                    if has_spoken:
                        audio_frames.extend(data)
                    else:
                        # Keep a tiny rolling buffer so the very first syllable isn't chopped off
                        audio_frames = bytearray(data)
                        
                    # --- BREAK CONDITIONS ---
                    
                    # 1. You finished speaking (waits for ~0.75 seconds of silence)
                    if has_spoken and trailing_silence >= 3:
                        break
                        
                    # 2. You never started speaking (times out after ~6 seconds of dead air)
                    if not has_spoken and idle_chunks >= 24:
                        print("[System]: Idle timeout reached. -> [BLUE LED OFF]")
                        return "<TIMEOUT>", None
            
            # --- SNAP PHOTO INSTANTLY ON SILENCE ---
            base64_image = None
            if is_vision_active:
                base64_image = self.capture_frame_base64()
                
            print("[System]: Transcribing voice... -> [BLUE LED OFF]")
            audio = sr.AudioData(bytes(audio_frames), samplerate, 2)
            text = r.recognize_google(audio)
            
            return text if text else None, base64_image
            
        except Exception as e:
            print(f"[STT Error]: {e}")
            return None, None

    def conversation_loop(self):
        while self.is_awake:
            question, base64_image = self.record_and_transcribe_in_ram(self.vision_mode)
            
            if question == "<TIMEOUT>":
                subprocess.Popen(["afplay", "/System/Library/Sounds/Basso.aiff"])
                self.chat_history = [{"role": "system", "content": SYSTEM_PROMPT}]
                self.is_awake = False
                break
            
            if not question:
                subprocess.Popen(["afplay", "/System/Library/Sounds/Basso.aiff"])
                continue
                
            print(f"[You]: {question}")
            lower_q = question.lower()
            
            if "start vision" in lower_q:
                print("[System]: Switching to Vision Mode.")
                subprocess.run(["say", "-r", "240", "Vision active."])
                self.vision_mode = True
                continue 
                
            if "start voice" in lower_q or "stop vision" in lower_q:
                print("[System]: Switching to Voice Mode.")
                subprocess.run(["say", "-r", "240", "Voice only."])
                self.vision_mode = False
                continue
                
            if any(word in lower_q for word in ["sleep", "quit", "bye", "shut down"]):
                subprocess.run(["say", "-r", "240", "Going offline."])
                self.chat_history = [{"role": "system", "content": SYSTEM_PROMPT}]
                self.vision_mode = False 
                self.is_awake = False
                break
                
            self.process_text_stream(question, base64_image)

    def run(self):
        print(f"Loaded Groq Cloud API '{MODEL_NAME}'")
        try:
            while True: time.sleep(1)
        except KeyboardInterrupt:
            sys.exit(0)

if __name__ == "__main__":
    app = AssistantApp()
    app.run()
