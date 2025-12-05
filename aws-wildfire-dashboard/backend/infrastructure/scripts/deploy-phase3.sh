#!/bin/bash
set -e

echo "🚀 Deploying AWS Wildfire Detection System - Phase 3: API Gateway"
echo "=================================================================="
echo ""

# Variables
REGION="us-east-2"
STACK_PREFIX="wildfire-sensor"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CF_DIR="${SCRIPT_DIR}/../cloudformation"

echo "Configuration:"
echo "  Region: $REGION"
echo "  Stack Prefix: $STACK_PREFIX"
echo ""

# Deploy API Gateway
echo "Step 1/1: Deploying API Gateway..."
aws cloudformation deploy \
  --template-file "${CF_DIR}/05-api-gateway.yaml" \
  --stack-name ${STACK_PREFIX}-api \
  --region $REGION \
  --no-fail-on-empty-changeset

if [ $? -eq 0 ]; then
    echo "✅ API Gateway deployed successfully"
else
    echo "❌ Failed to deploy API Gateway"
    exit 1
fi
echo ""

echo "=================================================================="
echo "✅ Phase 3 Deployment Complete!"
echo "=================================================================="
echo ""
echo "📊 API Endpoints:"
echo ""

# Get API outputs
API_ENDPOINT=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-api --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`ApiEndpoint`].OutputValue' --output text)
REALTIME_ENDPOINT=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-api --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`RealtimeEndpoint`].OutputValue' --output text)
HISTORICAL_ENDPOINT=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-api --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`HistoricalEndpoint`].OutputValue' --output text)
ALERTS_ENDPOINT=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-api --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`AlertsEndpoint`].OutputValue' --output text)

echo "Base URL: $API_ENDPOINT"
echo ""
echo "Endpoints:"
echo "  • GET /realtime    → $REALTIME_ENDPOINT"
echo "  • GET /historical  → $HISTORICAL_ENDPOINT"
echo "  • GET /alerts      → $ALERTS_ENDPOINT"
echo ""

# Get API Key
API_KEY_ID=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-api --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`ApiKeyId`].OutputValue' --output text)
API_KEY=$(aws apigateway get-api-key --api-key $API_KEY_ID --include-value --region $REGION --query 'value' --output text)

echo "🔑 API Key: $API_KEY"
echo ""
echo "⚠️  IMPORTANT: Save this API key! You'll need it for the frontend."
echo ""

echo "=================================================================="
echo "🧪 Testing API Endpoints"
echo "=================================================================="
echo ""

echo "Test 1: Real-time data"
echo "----------------------"
echo "curl -H \"x-api-key: $API_KEY\" $REALTIME_ENDPOINT"
echo ""
curl -s -H "x-api-key: $API_KEY" "$REALTIME_ENDPOINT" | jq '.' 2>/dev/null || curl -s -H "x-api-key: $API_KEY" "$REALTIME_ENDPOINT"
echo ""
echo ""

echo "Test 2: Alerts"
echo "--------------"
echo "curl -H \"x-api-key: $API_KEY\" $ALERTS_ENDPOINT"
echo ""
curl -s -H "x-api-key: $API_KEY" "$ALERTS_ENDPOINT" | jq '.' 2>/dev/null || curl -s -H "x-api-key: $API_KEY" "$ALERTS_ENDPOINT"
echo ""
echo ""

echo "=================================================================="
echo "📝 Save These Values for Frontend Configuration:"
echo "=================================================================="
echo ""
echo "# Add to frontend/.env file:"
echo "REACT_APP_API_ENDPOINT=$API_ENDPOINT"
echo "REACT_APP_API_KEY=$API_KEY"
echo ""
echo "=================================================================="
echo "Next Steps:"
echo "  - Test API endpoints with curl or Postman"
echo "  - Proceed to Phase 4: Frontend Dashboard deployment"
echo "  - Update frontend/.env with the values above"
echo "=================================================================="
